#include "base/config.hpp"
#include "base/log.hpp"
#include "base/assert.hpp"

#ifdef CONFIG_OS_FAMILY_UNIX
#include <cstring>
#endif

#include <csignal>
#include <cstdlib>
#include <exception>


namespace
{


extern "C" void signalHandler(int signal)
{
    LOG_INFO << "Signal caught: " << signal
#ifdef CONFIG_OS_FAMILY_UNIX
             << " (" << strsignal(signal) << ")"
#endif
#ifdef CONFIG_IS_DEBUG
             << '\n'
             << boost::stacktrace::stacktrace()
#endif
        ;
}

void atExitHandler()
{
    LOG_INFO << "atExitHandler called";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        base::initLog(base::LogLevel::ALL, base::Sink::STDOUT | base::Sink::FILE);
        LOG_INFO << "Application startup";

        for(auto signal_code: {SIGTERM, SIGSEGV, SIGINT, SIGILL, SIGABRT, SIGFPE}) {
            CHECK_SOFT(std::signal(signal_code, signalHandler) != SIG_ERR);
        }

        CHECK_SOFT(std::atexit(atExitHandler) == 0);

        return base::config::EXIT_OK;
    }
    catch(const std::exception& error) {
        LOG_ERROR << "Exception caught in main: " << error.what();
        return base::config::EXIT_FAIL;
    }
    catch(...) {
        LOG_ERROR << "Unknown exception caught";
        return base::config::EXIT_FAIL;
    }
}