#include <unistd.h>

#include "llvm/Support/FileSystem.h"

using namespace llvm;

std::string plugin_path;
bool is_cxx = false;
bool has_source = false;
std::string OutApiFile;
std::string OutSuccRetFile;
std::string OutLoopFile;
std::string OutStructNameFile;
std::vector<std::string> cc_params;

struct FlagSet {
  std::string flag;
  bool set;

  FlagSet(const std::string &flag) : flag(flag), set(false) {}
};

#define FLAG_SET(flag_name, flags)  \
static FlagSet flag_name(flags);  \
static inline bool check_ ## flag_name(StringRef arg) {  \
  if (arg.equals(flag_name.flag)) {  \
    flag_name.set = true; \
    return true;  \
  } \
  return false; \
} \
__attribute__((unused)) \
static inline bool flag_name ## _is_set() { \
  return flag_name.set; \
}

FLAG_SET(no_jump_table_flag, "-fno-jump-tables")
FLAG_SET(no_inline_flag, "-fno-inline")
FLAG_SET(optnone_disable_flag, "-disable-O0-optnone")
FLAG_SET(opt_level_0, "-O0")
FLAG_SET(opt_level_1, "-O1")
FLAG_SET(opt_level_2, "-O2")
FLAG_SET(opt_level_g, "-Og")
FLAG_SET(dbg_flag, "-g")
FLAG_SET(dwarf_version2_flag, "-gdwarf-2")
FLAG_SET(dwarf_version3_flag, "-gdwarf-3")
FLAG_SET(dwarf_version4_flag, "-gdwarf-4")
FLAG_SET(dwarf_version5_flag, "-gdwarf-5")

static int execvp_cxx(const std::string &file,
                      const std::vector<std::string> &argv) {
  std::vector<const char*> c_argv;
  for (auto &arg : argv) {
    c_argv.push_back(arg.c_str());
  }
  c_argv.push_back(NULL);
  return execvp(file.c_str(), (char *const *) c_argv.data());
}

inline
static void add_option(const std::string &opt) {
  cc_params.push_back("-Xclang");
  cc_params.push_back(opt);
}

static void find_obj(const std::string &cmd) {
  SmallString<128> path_vec;
  std::error_code err_code = sys::fs::real_path(cmd, path_vec, true);
  if (err_code) {
    errs() << "Failed to resolve real path for " << cmd << "\n";
    exit(1);
  }
  std::string path = path_vec.str().str();
  auto slash_idx = path.find_last_of('/');
  if (slash_idx != std::string::npos) {
    // found
    std::string dir = path.substr(0, slash_idx + 1);
    plugin_path = dir + "../lib/libperry-clang-plugin.so";
    return;
  }
  errs() << "Failed to locate path to perry clang plugin\n";
  exit(2);
}

static void check_name(const std::string &argv0) {
  std::string name(argv0);
  auto slash_idx = name.find_last_of('/');
  if (slash_idx != std::string::npos) {
    name = name.substr(0, slash_idx);
  }
  if (name == "perry-clang++") {
    is_cxx = true;
  }
  if (is_cxx) {
    cc_params.push_back("clang++");
  } else {
    cc_params.push_back("clang");
  }
}

static void check_target(const std::vector<std::string> &argv) {
  for (auto &arg : argv) {
    auto dot_index = arg.find_last_of('.');
    if (dot_index != std::string::npos) {
      auto sub_str = arg.substr(dot_index + 1);
      if (sub_str == "c" || sub_str == "cpp" || sub_str == "cc") {
        has_source = true;
        return;
      }
    }
  }
}
 
static void edit_params(const std::vector<std::string> &argv) {
  check_name(argv[0]);
  check_target(argv);

  // auto argc = argv.size();
  // if (argc == 2 && argv[1] == "-v") {
  //   do_add = false;
  // }

  std::vector<std::string> tmp_params;
  for (auto it = argv.begin() + 1, it_end = argv.end(); it != it_end; ++it) {
    StringRef arg(*it);
    if (arg.startswith("-out-api-file=")) {
      OutApiFile = arg.substr(sizeof("-out-api-file=") - 1);
      continue;
    }

    if (arg.startswith("-out-succ-ret-file=")) {
      OutSuccRetFile = arg.substr(sizeof("-out-succ-ret-file=") - 1);
      continue;
    }

    if (arg.startswith("-out-loop-file=")) {
      OutLoopFile = arg.substr(sizeof("-out-loop-file=") - 1);
      continue;
    }

    if (arg.startswith("-out-periph-struct-file=")) {
      OutStructNameFile = arg.substr(sizeof("-out-periph-struct-file=") - 1);
      continue;
    }

    tmp_params.push_back(*it);
  }

  if (has_source) {
    if (OutApiFile.empty()) {
      outs() << "No path given for the output API file, "
                "default to \'api.yaml\'\n";
      OutApiFile = "api.yaml";
    }
    if (OutSuccRetFile.empty()) {
      outs() << "No path given for the output Success return file, "
                "default to \'succ-ret.yaml\'\n";
      OutSuccRetFile = "succ-ret.yaml";
    }
    if (OutLoopFile.empty()) {
      outs() << "No path given for the output loops file, "
                "default to \'loops.yaml\'\n";
      OutLoopFile = "loops.yaml";
    }
    if (OutStructNameFile.empty()) {
      outs() << "No path given for the output periph struct name file, "
                "default to \'periph-struct.yaml\'\n";
      OutStructNameFile = "periph-struct.yaml";
    }

    add_option("-load");
    add_option(plugin_path);
    add_option("-add-plugin");
    add_option("perry");
    add_option("-plugin-arg-perry");
    add_option("-out-file-succ-ret");
    add_option("-plugin-arg-perry");
    add_option(OutSuccRetFile);
    add_option("-plugin-arg-perry");
    add_option("-out-file-api");
    add_option("-plugin-arg-perry");
    add_option(OutApiFile);
    add_option("-plugin-arg-perry");
    add_option("-out-file-loops");
    add_option("-plugin-arg-perry");
    add_option(OutLoopFile);
    add_option("-plugin-arg-perry");
    add_option("-out-file-periph-struct");
    add_option("-plugin-arg-perry");
    add_option(OutStructNameFile);

    // UBSan
    cc_params.push_back("-fsanitize=bounds");
    cc_params.push_back("-fsanitize=enum");
  }

  // disable inline
  if (!no_inline_flag_is_set()) {
    cc_params.push_back(no_inline_flag.flag);
  }

  // disable jump table
  if (!no_jump_table_flag_is_set()) {
    cc_params.push_back(no_jump_table_flag.flag);
  }

  // disable optnone
  if (!optnone_disable_flag_is_set()) {
    add_option(optnone_disable_flag.flag);
  }

  // preserve dbg info
  if (!dbg_flag_is_set()) {
    cc_params.push_back(dbg_flag.flag);
  }

  // set dwarf version
  if (!dwarf_version4_flag_is_set()) {
    cc_params.push_back(dwarf_version4_flag.flag);
  }

  // set opt level
  if (!opt_level_g_is_set()) {
    cc_params.push_back(opt_level_g.flag);
  }

  cc_params.insert(cc_params.end(), tmp_params.begin(), tmp_params.end());
}

int main(int argc, char* argv[]) {
  std::vector<std::string> _argv;
  for (int i = 0; i < argc; ++i) {
    StringRef arg(argv[i]);
    check_no_jump_table_flag(arg);
    check_no_inline_flag(arg);
    check_optnone_disable_flag(arg);
    check_dbg_flag(arg);
    check_dwarf_version4_flag(arg);
    if (check_dwarf_version2_flag(arg)  ||
        check_dwarf_version3_flag(arg)  ||
        check_dwarf_version5_flag(arg)) {
      continue;
    }
    check_opt_level_g(arg);
    if (check_opt_level_0(arg)  ||
        check_opt_level_1(arg)  ||
        check_opt_level_2(arg)) {
      continue;
    }
    
    _argv.push_back(std::string(argv[i]));
  }
  find_obj(_argv[0]);
  edit_params(_argv);

  // std::string cmdline;
  // raw_string_ostream OS(cmdline);
  // for (auto &arg : cc_params) {
  //   OS << arg << " ";
  // }
  // outs() << cmdline << "\n";

  int ret = execvp_cxx(cc_params[0], cc_params);

  errs() << "Failed to execute " << cc_params[0]
         << ": " << strerror(errno) << "\n";
  return ret;
}