require "rack/utils"
require "json"
require "open3"
require "coderay"
require "fileutils"

CANDIDATES_PATH  = '../wisdom-builder/data/6/javascript.candidates'
VOCAB_PATH       = '../wisdom-builder/data/6/javascript.candidates.vocab'
LM_PATH          = '../wisdom-builder/data/6/javascript.model'

KENLM_QUERY_PATH = 'kenlm/bin/wisdom_query'
TIMEOUT          = 6.0
MAX_SUGGESTIONS  = 20

LEXICAL_PUSHES = {
  0 => 0,
  1 => 0.1,
  2 => 0.2,
  3 => 0.3,
  4 => 0.4,
  5 => 0.5,
  6 => 0.6,
  7 => 0.7,
  8 => 0.8,
  9 => 0.9,
}
LEXICAL_PUSHES.default = 1

def tokenize(code)
  current_group = nil
  res = []

  CodeRay.
  scan(code, :javascript).
  tokens.each_slice(2){ |token, kind|
    if current_group && token != :end_group
      current_group << token
    else
      case token
      when :begin_group
        current_group = []
      when :end_group
        if current_group
          res << current_group.join
          current_group = nil
        end
      else # when String
        case kind
        when :comment
          # nothing
        when :space
          if token.include?("\n") && res[-1] != "<s/>"
            res << "<s/>"
          else
            res << " "
          end
        else
          res << token
        end
      end
    end
  }

  res << current_group.join if current_group

  ["<s>"] + res
end

def load_candidates!(path = CANDIDATES_PATH)
  @candidates = JSON.load(File.read(path))
end

def load_vocab!(path = VOCAB_PATH)
  @vocab = File.read(path).split("\0")
end

@cache = {}

def get_suggestions_from_cache(considered_tokens, current, cache = @cache)
  cache[considered_tokens + [current]]
end

def cache_suggestion_and_return(considered_tokens, current, suggestions, cache = @cache)
  cache[considered_tokens + [current]] = suggestions
end

def get_suggestions(tokens = [], current = nil, cache = @cache, candidates_ = @candidates, vocab = @vocab)
  considered_tokens = tokens.last(6)
  if cache_hit = get_suggestions_from_cache(considered_tokens, current, cache)
    puts "CACHE HIT"
    return cache_hit
  end

  return [] if considered_tokens.empty?

  worst_suggestion_prob = 100
  suggestions = {}
  skip_cache = false
  vocab_index = vocab.index tokens[-1]
  candidate_indexes = candidates_["#{vocab_index || vocab.index("<unk>")}"]# || candidates_[vocab.index("<unk>")]

  candidate_indexes.compact!

  if candidate_indexes
    candidates = candidate_indexes.map{ |e| vocab[e] }#.compact
    puts "current: #{current}"
    candidates = candidates.reject{ |candidate|
      candidate == "" || current && !candidate.start_with?(current)
    }

    query_string = "#{considered_tokens.join(" ")}".gsub(/'/, '\\\\\'')
    candidate_probs = ""

    candidate_slices = candidates.each_slice(5000).to_a
    timeout = TIMEOUT / candidate_slices.size
    candidate_slices.each{ |cs|
      exit_status = nil
      Open3.popen2("timeout", "%.2f" % timeout, KENLM_QUERY_PATH, "-q", query_string, LM_PATH){ |in_, out, w|
        in_ << cs.map{ |c| c + "\n" }.join
        in_.close
        candidate_probs << out.read
        exit_status = w.value
      }
      unless exit_status.success?
        puts "GOT TIMEOUT #{timeout}"
        skip_cache = true
        candidate_probs = ""
        break
      end
    }

    unless candidate_probs.empty?
      lexical_map = get_lexical_map(tokens)
      candidates.zip(candidate_probs.split("\n")).each{ |candidate, prob_raw|
        begin
          prob = - Float(prob_raw)
        rescue ArgumentError, TypeError
          next
        end

        prob = prob - (LEXICAL_PUSHES[lexical_map[candidate]] * prob)

        if prob < worst_suggestion_prob || suggestions.size < MAX_SUGGESTIONS
          suggestions[candidate] = prob
          if suggestions.size > MAX_SUGGESTIONS
            worst_suggestion = suggestions.key(suggestions.values.max)
            suggestions.delete(worst_suggestion)
          end
          worst_suggestion_prob = suggestions.values.max
        end
      }
    else
      # no probs
    end
  else
    puts "no candidates found"
  end

  puts "done", suggestions.sort_by{ |k,v| v }.map{ |k,v| k }

  sorted_suggestions = suggestions.sort_by{ |k,v| v }
  if skip_cache
    sorted_suggestions
  else
    cache_suggestion_and_return(considered_tokens, current, sorted_suggestions, cache)
  end
end

def build_response(env)
  request = Rack::Request.new(env)
  payload = request[:payload]
  return unless payload
  params = JSON.load(payload)
  position = params["position"]
  buffer   = params["buffer"]

  buffer_till_cursor = buffer[/\A(?:.*\n){#{position["row"]}}.{#{position["column"]}}/]
  return [] unless buffer_tokenized = tokenize(buffer_till_cursor)
  last_token_no_space = buffer_tokenized[-1] !~ /\A\s+\z/ && buffer_tokenized[-1] != "<s/>"
  buffer_tokenized.select!{ |token| token !~ /\A\s*\z/}
  if last_token_no_space && buffer_tokenized.size > 1 && token_is_lexical?(buffer_tokenized[-1])
    prefix = buffer_tokenized[-1]
    buffer_tokenized = buffer_tokenized[0..-2]
  else
    prefix = nil
  end

  if prefix
    suggestions = get_suggestions(buffer_tokenized, prefix)
    if suggestions[0] && suggestions[0][0] == prefix && ( suggestions.size == 1 || suggestions[0][1] < 0.3 ) # ~ 50%
      suggestions = get_suggestions(buffer_tokenized + [prefix])
      prefix = nil
    end
  else
    suggestions = get_suggestions(buffer_tokenized)
  end

  res = {
    suggestions: suggestions.map{ |suggestion, prob|
      real_prob = 10**-prob
      if real_prob < 0.00001
        friendly_prob = "< 0.001%"
      else
        friendly_prob   = "%.3f%%" %(real_prob*100)
      end

      if suggestion == "<s/>" # new-line
        {
          text: "\n",
          displayText: "⏎",
          rightLabel: friendly_prob,
          className: "prob-#{ real_prob.round(1).to_s.tr('.', '-') }",
          replacementPrefix: "",
        }
      else
        {
          text: suggestion,
          rightLabel: friendly_prob,
          description: "Search for #{suggestion} on GitHub:",
          descriptionMoreURL: "https://github.com/search?q=#{Rack::Utils.escape(suggestion)}&type=Code",
          className: "prob-#{ real_prob.round(1).to_s.tr('.', '-') }",
          replacementPrefix: prefix || "",
        }
      end

    }
  }

  [200, JSON.dump(res)]
end

def token_is_lexical?(token)
  token =~ /\A[$_[:alnum:]]+\z/ || (
    token[0] =~ /[\/'"]/ && ( token[-1] != $& || token.size == 1 )
  )
end

def get_lexical_map(tokens)
  res = Hash.new(0)
  tokens.each{ |token| res[token] += 1 if token_is_lexical?(token) }
  res
end

puts "WisdomComplete: Loading vocabulary"
load_vocab!

puts "WisdomComplete: Loading candidates"
load_candidates!

puts "WisdomComplete: Starting server"

run Proc.new { |env|
  success, response = build_response(env)
  if success
    [200, {"Content-Type" => "text/html"}, [response]]
  else
    [301, {"Location" => response, "Content-Length" => "0"}, []]
  end
}

