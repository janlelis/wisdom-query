#ifndef LM_NGRAM_QUERY_H
#define LM_NGRAM_QUERY_H

#include "lm/enumerate_vocab.hh"
#include "lm/model.hh"
#include "util/file_piece.hh"
#include "util/usage.hh"

#include <cstdlib>
#include <iostream>
#include <ostream>
#include <istream>
#include <string>
#include <sstream>

#include <math.h>

namespace lm {
namespace ngram {

struct BasicPrint {
  void Word(StringPiece, WordIndex, const FullScoreReturn &) const {}
  void Line(uint64_t oov, float total) const {
    std::cout << "Total: " << total << " OOV: " << oov << '\n';
  }
  void Summary(double, double, uint64_t, uint64_t) {}

};

struct WisdomPrint {
  void Word(StringPiece, WordIndex, const FullScoreReturn &) const {}
  void Line(uint64_t oov, float total) const {
    std::cout << total << '\n';
  }
  void Summary(double, double, uint64_t, uint64_t) {}
};

struct FullPrint : public BasicPrint {
  void Word(StringPiece surface, WordIndex vocab, const FullScoreReturn &ret) const {
    std::cout << surface << '=' << vocab << ' ' << static_cast<unsigned int>(ret.ngram_length)  << ' ' << ret.prob << '\t';
  }

  void Summary(double ppl_including_oov, double ppl_excluding_oov, uint64_t corpus_oov, uint64_t corpus_tokens) {
    std::cout <<
      "Perplexity including OOVs:\t" << ppl_including_oov << "\n"
      "Perplexity excluding OOVs:\t" << ppl_excluding_oov << "\n"
      "OOVs:\t" << corpus_oov << "\n"
      "Tokens:\t" << corpus_tokens << '\n'
      ;
  }
};

template <class Model, class Printer> void Query(const Model &model, bool sentence_context) {
  Printer printer;
  typename Model::State state, out;
  lm::FullScoreReturn ret;
  StringPiece word;

  util::FilePiece in(0);

  double corpus_total = 0.0;
  double corpus_total_oov_only = 0.0;
  uint64_t corpus_oov = 0;
  uint64_t corpus_tokens = 0;

  while (true) {
    state = sentence_context ? model.BeginSentenceState() : model.NullContextState();
    float total = 0.0;
    uint64_t oov = 0;

    while (in.ReadWordSameLine(word)) {
      lm::WordIndex vocab = model.GetVocabulary().Index(word);
      ret = model.FullScore(state, vocab, out);
      if (vocab == model.GetVocabulary().NotFound()) {
        ++oov;
        corpus_total_oov_only += ret.prob;
      }
      total += ret.prob;
      printer.Word(word, vocab, ret);
      ++corpus_tokens;
      state = out;
    }
    // If people don't have a newline after their last query, this won't add a </s>.
    // Sue me.
    try {
      UTIL_THROW_IF('\n' != in.get(), util::Exception, "FilePiece is confused.");
    } catch (const util::EndOfFileException &e) { break; }
    if (sentence_context) {
      ret = model.FullScore(state, model.GetVocabulary().EndSentence(), out);
      total += ret.prob;
      ++corpus_tokens;
      printer.Word("</s>", model.GetVocabulary().EndSentence(), ret);
    }
    printer.Line(oov, total);
    corpus_total += total;
    corpus_oov += oov;
  }
  printer.Summary(
      pow(10.0, -(corpus_total / static_cast<double>(corpus_tokens))), // PPL including OOVs
      pow(10.0, -((corpus_total - corpus_total_oov_only) / static_cast<double>(corpus_tokens - corpus_oov))), // PPL excluding OOVs
      corpus_oov,
      corpus_tokens);
}

template <class Model, class Printer> void WisdomQuery(const Model &model, const char *query) {
  Printer printer;
  typename Model::State state, state2;
  lm::FullScoreReturn ret;
  StringPiece word;

  util::FilePiece in(0);

  state = model.NullContextState();
  float total = 0.0;

  // build up context
  std::istringstream qss(query);
  std::string query_word;
  while(qss >> query_word) {
    lm::WordIndex vocab = model.GetVocabulary().Index(query_word);
    ret = model.FullScore(state, vocab, state2);
    total += ret.prob;
    state = state2;
  }

  // compare candidates
  while (true) {
    while (in.ReadWordSameLine(word)) {
      lm::WordIndex vocab = model.GetVocabulary().Index(word);
      ret = model.FullScore(state, vocab, state2);
      // std::cout << "Candidate " << word << ": " << ret.prob << '\n';
      std::cout << ret.prob << '\n';
    }
    try {
      UTIL_THROW_IF('\n' != in.get(), util::Exception, "FilePiece is confused.");
    } catch (const util::EndOfFileException &e) { break; }

    // printer.Line(0, candidate);
  }
}
/*
    self.model.NullContextWrite(&state)
cdef State out_state
cdef float total = 0
for word in words:
    total += self.model.BaseScore(&state, self.vocab.Index(word), &out_state)
    state = out_state
    */


template <class Model> void Query(const char *file, const Config &config, bool sentence_context, bool show_words) {
  Model model(file, config);
  if (show_words) {
    Query<Model, FullPrint>(model, sentence_context);
  } else {
    Query<Model, BasicPrint>(model, sentence_context);
  }
}

template <class Model> void WisdomQuery(const char *file, const Config &config, const char *query) {
  Model model(file, config);
  WisdomQuery<Model, WisdomPrint>(model, query);
}

} // namespace ngram
} // namespace lm

#endif // LM_NGRAM_QUERY_H


