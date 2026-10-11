#include "core/Process.h"

#ifdef _WIN32
#include <windows.h>
#include "core/StringUtil.h"
#else
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace vt {

Process::~Process() {
  kill();
  if (reader_.joinable()) reader_.join();
}

#ifdef _WIN32

namespace {

// Quotes one argument per the MSVC CRT command-line parsing rules.
std::wstring quoteArg(const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
  std::wstring out = L"\"";
  for (size_t i = 0;; ++i) {
    size_t backslashes = 0;
    while (i < arg.size() && arg[i] == L'\\') {
      ++i;
      ++backslashes;
    }
    if (i == arg.size()) {
      out.append(backslashes * 2, L'\\');
      break;
    }
    if (arg[i] == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
    } else {
      out.append(backslashes, L'\\');
      out.push_back(arg[i]);
    }
  }
  out.push_back(L'"');
  return out;
}

}  // namespace

bool Process::start(const std::filesystem::path& program, const std::vector<std::string>& args,
                    const std::filesystem::path& workingDir, LineFn onLine, ExitFn onExit, std::string& error) {
  if (running_) {
    error = "process already running";
    return false;
  }
  if (reader_.joinable()) reader_.join();

  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE readEnd = nullptr, writeEnd = nullptr;
  if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
    error = "CreatePipe failed";
    return false;
  }
  SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

  std::wstring cmd = quoteArg(program.wstring());
  for (const auto& a : args) cmd += L" " + quoteArg(widen(a));

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = writeEnd;
  si.hStdError = writeEnd;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};

  std::wstring cwd = workingDir.wstring();
  BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                           nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
  CloseHandle(writeEnd);
  if (!ok) {
    error = "CreateProcess failed (" + std::to_string(GetLastError()) + ") for " + narrow(program.wstring());
    CloseHandle(readEnd);
    return false;
  }

  // Put the child in a job so killing the job takes down grandchildren too.
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (job) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    AssignProcessToJobObject(job, pi.hProcess);
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);

  process_ = pi.hProcess;
  job_ = job;
  readPipe_ = readEnd;
  onLine_ = std::move(onLine);
  onExit_ = std::move(onExit);
  exitCode_ = -1;
  running_ = true;
  reader_ = std::thread([this] { readerLoop(); });
  return true;
}

void Process::readerLoop() {
  std::string pending;
  char buf[4096];
  DWORD n = 0;
  while (ReadFile(static_cast<HANDLE>(readPipe_), buf, sizeof(buf), &n, nullptr) && n > 0) {
    pending.append(buf, n);
    size_t pos;
    while ((pos = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, pos);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (onLine_) onLine_(line);
      pending.erase(0, pos + 1);
    }
  }
  if (!pending.empty() && onLine_) onLine_(pending);
  WaitForSingleObject(static_cast<HANDLE>(process_), INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(static_cast<HANDLE>(process_), &code);
  exitCode_ = static_cast<int>(code);
  CloseHandle(static_cast<HANDLE>(readPipe_));
  CloseHandle(static_cast<HANDLE>(process_));
  if (job_) CloseHandle(static_cast<HANDLE>(job_));
  readPipe_ = process_ = job_ = nullptr;
  running_ = false;
  if (onExit_) onExit_(exitCode_);
}

void Process::kill() {
  if (!running_) return;
  if (job_) {
    TerminateJobObject(static_cast<HANDLE>(job_), 1);
  } else if (process_) {
    TerminateProcess(static_cast<HANDLE>(process_), 1);
  }
}

#else  // POSIX

bool Process::start(const std::filesystem::path& program, const std::vector<std::string>& args,
                    const std::filesystem::path& workingDir, LineFn onLine, ExitFn onExit, std::string& error) {
  if (running_) {
    error = "process already running";
    return false;
  }
  if (reader_.joinable()) reader_.join();

  int fds[2];
  if (pipe(fds) != 0) {
    error = "pipe failed";
    return false;
  }
  std::vector<std::string> argvStore;
  argvStore.push_back(program.string());
  for (const auto& a : args) argvStore.push_back(a);
  std::vector<char*> argv;
  for (auto& a : argvStore) argv.push_back(a.data());
  argv.push_back(nullptr);

  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    error = "fork failed";
    return false;
  }
  if (pid == 0) {
    setpgid(0, 0);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[0]);
    close(fds[1]);
    if (!workingDir.empty() && chdir(workingDir.c_str()) != 0) _exit(127);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  close(fds[1]);
  pid_ = pid;
  readFd_ = fds[0];
  onLine_ = std::move(onLine);
  onExit_ = std::move(onExit);
  exitCode_ = -1;
  running_ = true;
  reader_ = std::thread([this] { readerLoop(); });
  return true;
}

void Process::readerLoop() {
  std::string pending;
  char buf[4096];
  ssize_t n;
  while ((n = read(readFd_, buf, sizeof(buf))) > 0) {
    pending.append(buf, static_cast<size_t>(n));
    size_t pos;
    while ((pos = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, pos);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (onLine_) onLine_(line);
      pending.erase(0, pos + 1);
    }
  }
  if (!pending.empty() && onLine_) onLine_(pending);
  int status = 0;
  waitpid(pid_, &status, 0);
  exitCode_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  close(readFd_);
  readFd_ = -1;
  pid_ = -1;
  running_ = false;
  if (onExit_) onExit_(exitCode_);
}

void Process::kill() {
  if (!running_ || pid_ <= 0) return;
  ::kill(-pid_, SIGTERM);
}

#endif

int Process::wait() {
  if (reader_.joinable()) reader_.join();
  return exitCode_;
}

}  // namespace vt
