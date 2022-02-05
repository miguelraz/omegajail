#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <string_view>
#include <vector>

#include "logging.h"
#include "util.h"

namespace {

std::string TrimExtension(std::string_view filename,
                          std::string_view extension) {
  if (filename.size() >= extension.size() &&
      filename.compare(filename.size() - extension.size(), std::string::npos,
                       extension) == 0) {
    filename.remove_suffix(extension.size());
  }
  return std::string(filename);
}

[[noreturn]] void Exec(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (const std::string& arg : args)
    argv.emplace_back(const_cast<char*>(arg.data()));
  argv.emplace_back(nullptr);
  if (execve(argv[0], argv.data(), environ))
    PLOG(FATAL) << "Failed to execve `" << StringJoin(args, " ") << "`";
  abort();
}

int ForkExec(const std::vector<std::string>& args) {
  pid_t child = vfork();
  if (child == -1)
    PLOG(FATAL) << "Could not fork child";

  if (child == 0) {
    // This is the child process.
    Exec(args);
  }

  // Parent process.
  int wstatus;
  while (true) {
    if (waitpid(child, &wstatus, WUNTRACED | WCONTINUED) == -1)
      PLOG(FATAL) << "Failed to wait for child";

    if (WIFEXITED(wstatus))
      return WEXITSTATUS(wstatus);
    if (WIFSIGNALED(wstatus))
      return WTERMSIG(wstatus);
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  bool kotlin = false;
  if (argc >= 2 && std::string_view(argv[1]).rfind("--language=") == 0) {
    if (std::string_view(argv[1]) == "--language=kotlin") {
      kotlin = true;
    }
    argc--;
    argv[1] = argv[0];
    argv++;
  }
  if (argc < 3) {
    PLOG(FATAL) << argv[0]
                << " [--language=...] <target> <source> [<source> ...]";
  }
  std::vector<std::string> compiler_args;
  std::vector<std::string> native_image_args = {
      "/usr/lib/jvm/graalvm/bin/native-image",
      "-dsa",
      "-H:NumberOfThreads=1",
      "-J-Xms512M",
      "-J-Xmx896M",
  };
  if (kotlin) {
    compiler_args = {
        "/usr/bin/java",
        "-Xmx896M",
        "-Xms32M",
        "-XX:+UseSerialGC",
        "-cp",
        "/usr/lib/jvm/kotlinc/lib/kotlin-preloader.jar",
        "org.jetbrains.kotlin.preloading.Preloader",
        "-cp",
        "/usr/lib/jvm/kotlinc/lib/kotlin-compiler.jar",
        "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler",
    };
    native_image_args.emplace_back("-cp");
    native_image_args.emplace_back(
        "/usr/lib/jvm/kotlinc/lib/kotlin-stdlib.jar:.");
  } else {
    compiler_args = {
        "/usr/bin/javac",
        "-J-Xmx896M",
        "-J-Xms32M",
        "-J-XX:+UseSerialGC",
    };
  }
  compiler_args.emplace_back("-d");
  compiler_args.emplace_back(".");
  for (int i = 2; i < argc; ++i) {
    compiler_args.emplace_back(argv[i]);
    if (kotlin) {
      native_image_args.emplace_back(
          StringPrintf("%sKt", TrimExtension(argv[i], ".kt").data()));
    } else {
      native_image_args.emplace_back(TrimExtension(argv[i], ".java").data());
    }
  }
  native_image_args.emplace_back(argv[1]);

  int status = ForkExec(compiler_args);
  if (status != 0)
    return status;
  Exec(native_image_args);
}
