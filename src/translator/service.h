#ifndef SRC_BERGAMOT_SERVICE_H_
#define SRC_BERGAMOT_SERVICE_H_

#include "batch_translator.h"
#include "batcher.h"
#include "data/types.h"
#include "response.h"
#include "text_processor.h"
#include "translator/parser.h"

#ifndef WASM_COMPATIBLE_SOURCE
#include "pcqueue.h"
#endif

#include <queue>
#include <vector>

namespace marian {
namespace bergamot {

/// Service exposes methods to translate an incoming blob of text to the
/// Consumer of bergamot API.
///
/// An example use of this API looks as follows:
///
///  options = ...;
///  service = Service(options);
///  std::string input_text = "Hello World";
///  std::future<Response>
///      response = service.translate(std::move(input_text));
///  response.wait();
///  Response result = response.get();
///
/// Optionally Service can be initialized by also passing model_memory for
/// purposes of efficiency (which defaults to nullpointer and then reads from
/// file supplied through config).
class Service {

public:
  /// @param options Marian options object
  /// @param modelMemory byte array (aligned to 256!!!) that contains the bytes
  /// of a model.bin. Optional, defaults to nullptr when not used
  /// @param shortlistMemory byte array of shortlist (aligned to 64)
  explicit Service(Ptr<Options> options, AlignedMemory modelMemory, AlignedMemory shortlistMemory);

  explicit Service(Ptr<Options> options) : Service(options, AlignedMemory(), AlignedMemory()){}

  /// Construct Service from a string configuration.
  /// @param [in] config string parsable as YAML expected to adhere with marian
  /// config
  /// @param [in] model_memory byte array (aligned to 256!!!) that contains the
  /// bytes of a model.bin. Optional.
  /// @param [in] shortlistMemory byte array of shortlist (aligned to 64)
  explicit Service(const std::string &config,
                   AlignedMemory modelMemory = AlignedMemory(), AlignedMemory shortlistMemory = AlignedMemory())
      : Service(parseOptions(config), std::move(modelMemory), std::move(shortlistMemory)) {}

  /// Explicit destructor to clean up after any threads initialized in
  /// asynchronous operation mode.
  ~Service();

  /// To stay efficient and to refer to the string for alignments, expects
  /// ownership be moved through std::move(..)
  ///
  ///  @param [in] rvalue reference of string to be translated.
  std::future<Response> translate(std::string &&input);

private:
  /// Build numTranslators number of translators with options from options
  void build_translators(Ptr<Options> options, size_t numTranslators);
  /// Initializes a blocking translator without using std::thread
  void initialize_blocking_translator();
  /// Translates through direct interaction between batcher_ and translators_
  void blocking_translate();

  /// Launches multiple workers of translators using std::thread
  /// Reduces to ABORT if called when not compiled WITH_PTHREAD
  void initialize_async_translators();
  /// Async translate produces to a producer-consumer queue as batches are
  /// generated by Batcher. In another thread, the translators consume from
  /// producer-consumer queue.
  /// Reduces to ABORT if called when not compiled WITH_PTHREAD
  void async_translate();

  /// Number of workers to launch.
  size_t numWorkers_;              // ORDER DEPENDENCY (pcqueue_)
  /// Model memory to load model passed as bytes.
  AlignedMemory modelMemory_;      // ORDER DEPENDENCY (translators_)
  /// Shortlist memory passed as bytes.
  AlignedMemory shortlistMemory_;  // ORDER DEPENDENCY (translators_)

  /// Holds instances of batch translators, just one in case
  /// of single-threaded application, numWorkers_ in case of multithreaded
  /// setting.
  std::vector<BatchTranslator> translators_;  // ORDER DEPENDENCY (modelMemory_, shortlistMemory_)

  /// Stores requestId of active request. Used to establish
  /// ordering among requests and logging/book-keeping.

  size_t requestId_;

  /// Store vocabs representing source and target.
  std::vector<Ptr<Vocab const>> vocabs_; // ORDER DEPENDENCY (text_processor_)

  /// TextProcesser takes a blob of text and converts into format consumable by
  /// the batch-translator and annotates sentences and words.
  TextProcessor text_processor_; // ORDER DEPENDENCY (vocabs_)

  /// Batcher handles generation of batches from a request, subject to
  /// packing-efficiency and priority optimization heuristics.
  Batcher batcher_;

  // The following constructs are available providing full capabilities on a non
  // WASM platform, where one does not have to hide threads.
#ifndef WASM_COMPATIBLE_SOURCE
  PCQueue<Batch> pcqueue_; // ORDER DEPENDENCY (numWorkers_)
  std::vector<std::thread> workers_;
#endif // WASM_COMPATIBLE_SOURCE
};

} // namespace bergamot
} // namespace marian

#endif // SRC_BERGAMOT_SERVICE_H_