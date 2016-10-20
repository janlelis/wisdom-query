#include "lm/ngram_query.hh"
#include "util/getopt.hh"

#ifdef WITH_NPLM
#include "lm/wrappers/nplm.hh"
#endif

#include <stdlib.h>

void Usage(const char *name) {
  std::cerr <<
    "KenLM Query for Wisdom (" << KENLM_MAX_ORDER << ").\n"
    "Usage: " << name << " lm_file\n"
    "-l lazy|populate|read|parallel: Load lazily, with populate, or malloc+read\n"
    "The default loading method is populate on Linux and read on others.\n";
  exit(1);
}

int main(int argc, char *argv[]) {
  std::string query;

  if (argc == 1 || (argc == 2 && !strcmp(argv[1], "--help")))
    Usage(argv[0]);

  lm::ngram::Config config;

  int opt;
  while ((opt = getopt(argc, argv, "hq:l:")) != -1) {
    switch (opt) {
      case 'q':
        query = optarg;
        break;
      case 'l':
        if (!strcmp(optarg, "lazy")) {
          config.load_method = util::LAZY;
        } else if (!strcmp(optarg, "populate")) {
          config.load_method = util::POPULATE_OR_READ;
        } else if (!strcmp(optarg, "read")) {
          config.load_method = util::READ;
        } else if (!strcmp(optarg, "parallel")) {
          config.load_method = util::PARALLEL_READ;
        } else {
          Usage(argv[0]);
        }
        break;
      case 'h':
      default:
        Usage(argv[0]);
    }
  }
  if (optind + 1 != argc)
    Usage(argv[0]);
  const char *file = argv[optind];
  try {
    using namespace lm::ngram;
    ModelType model_type;
    if (RecognizeBinary(file, model_type)) {
      switch(model_type) {
        case PROBING:
          WisdomQuery<lm::ngram::ProbingModel>(file, config, query.c_str());
          break;
        case REST_PROBING:
          WisdomQuery<lm::ngram::RestProbingModel>(file, config, query.c_str());
          break;
        case TRIE:
          WisdomQuery<TrieModel>(file, config, query.c_str());
          break;
        case QUANT_TRIE:
          WisdomQuery<QuantTrieModel>(file, config, query.c_str());
          break;
        case ARRAY_TRIE:
          WisdomQuery<ArrayTrieModel>(file, config, query.c_str());
          break;
        case QUANT_ARRAY_TRIE:
          WisdomQuery<QuantArrayTrieModel>(file, config, query.c_str());
          break;
        default:
          std::cerr << "Unrecognized kenlm model type " << model_type << std::endl;
          abort();
      }
#ifdef WITH_NPLM
    } else if (lm::np::Model::Recognize(file)) {
      lm::np::Model model(file);
      Query<lm::np::Model, lm::ngram::BasicPrint>(model, false);
#endif
    } else {
      WisdomQuery<ProbingModel>(file, config, query.c_str());
    }
    // util::PrintUsage(std::cerr);
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
  return 0;
}
