#include "llvm/ADT/ArrayRef.h"
#include "llvm/Passes/PassBuilder.h"

#include <functional>

using namespace llvm;

void registerRaptor(llvm::PassBuilder &PB);

extern "C" int optMain(int argc, char **argv,
                       llvm::ArrayRef<std::function<void(llvm::PassBuilder &)>>
                           PassBuilderCallbacks);

int main(int argc, char **argv) {
  std::function<void(llvm::PassBuilder &)> plugins[] = {registerRaptor};
  return optMain(argc, argv, plugins);
}
