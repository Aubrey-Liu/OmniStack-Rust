#ifndef __OMNISTACK_LOGGER_H
#define __OMNISTACK_LOGGER_H

#include <iostream>
#include <ostream>
#include <filesystem>
#include <fstream>

#include <ctime>
#include <chrono>
#include <mutex>

#ifdef _MSC_VER_ // for MSVC
#define omni_forceinline __forceinline
#elif defined __GNUC__ // for gcc on Linux/Apple OS X
#define omni_forceinline __inline__ __attribute__((always_inline))
#else
#define omni_forceinline
#endif

#define OMNI_NONE         "\033[m"
#define OMNI_RED          "\033[0;32;31m"
#define OMNI_LIGHT_RED    "\033[1;31m"
#define OMNI_GREEN        "\033[0;32;32m"
#define OMNI_LIGHT_GREEN  "\033[1;32m"
#define OMNI_BLUE         "\033[0;32;34m"
#define OMNI_LIGHT_BLUE   "\033[1;34m"
#define OMNI_DARY_GRAY    "\033[1;30m"
#define OMNI_CYAN         "\033[0;36m"
#define OMNI_LIGHT_CYAN   "\033[1;36m"
#define OMNI_PURPLE       "\033[0;35m"
#define OMNI_LIGHT_PURPLE "\033[1;35m"
#define OMNI_BROWN        "\033[0;33m"
#define OMNI_YELLOW       "\033[1;33m"
#define OMNI_LIGHT_GRAY   "\033[0;37m"
#define OMNI_WHITE        "\033[1;37m"

#define OMNI_LOG(level) omnistack::common::Logger::Log(level, nullptr, __FILE__, __LINE__)
#define OMNI_LOG_TAG(level, tag) omnistack::common::Logger::Log(level, tag, __FILE__, __LINE__)

namespace omnistack::common {

    static inline std::string GetDate() {
        auto now = std::chrono::system_clock::now();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
        auto sec = static_cast<time_t>(seconds.count());
        std::tm tm;
        ::gmtime_r(&sec, &tm);

        char buff[32] = {0};
        auto size = std::strftime(buff, sizeof(buff), "%Y-%m-%d_%H:%M:%S", &tm);
        return std::string(buff, size);
    }

    static inline std::string GetDateWithMs() {
        auto now = std::chrono::system_clock::now();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) -
            seconds;
        auto sec = static_cast<time_t>(seconds.count());
        std::tm tm;
        ::gmtime_r(&sec, &tm);

        char buff[32] = {0};
        auto size = std::strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", &tm);
        return std::string(buff, size) + "." + std::to_string(milliseconds.count());
    }

    static inline std::string GetIpv4Str(const uint32_t& addr) {
        std::string ret;
        ret += std::to_string(addr & 0xFF) + ".";
        ret += std::to_string(addr >> 8 & 0xFF) + ".";
        ret += std::to_string(addr >> 16 & 0xFF) + ".";
        ret += std::to_string(addr >> 24 & 0xFF);
        return ret;
    }

    enum LogLevel {
        kDebug,
        kInfo,
        kWarning,
        kError,
        kFatal,
    };

    /**
     * [OmniStack] [LogLevel] {[Tag]} [Time] [File:Line] [Message]
    */

    class LockedStream {
    public:
        LockedStream(std::ostream* stream, std::ostream* fstream,
            LogLevel level, const char* tag, const char* position, const int line) {
            stream_ = stream;
            fstream_ = fstream;
            lock_.lock();

            std::string output = OMNI_YELLOW + std::string("[OmniStack] ") + OMNI_NONE;
            std::string file_output = "[OmniStack] ";

            switch (level) {
            case kDebug:
                output += OMNI_LIGHT_GRAY + std::string("[Debug] ") + OMNI_NONE;
                file_output += "[Debug] ";
                break;
            case kInfo:
                output += OMNI_LIGHT_GREEN + std::string("[Info] ") + OMNI_NONE;
                file_output += "[Info] ";
                break;
            case kWarning:
                output += OMNI_YELLOW + std::string("[Warning] ") + OMNI_NONE;
                file_output += "[Warning] ";
                break;
            case kError:
                output += OMNI_LIGHT_RED + std::string("[Error] ") + OMNI_NONE;
                file_output += "[Error] ";
                break;
            case kFatal:
                output += OMNI_RED + std::string("[Fatal] ") + OMNI_NONE;
                file_output += "[Fatal] ";
                break;
            }

            if (tag) {
                output += OMNI_NONE + std::string("[") + tag + std::string("] ") + OMNI_NONE;
                file_output += std::string("[") + tag + std::string("] ");
            }

            output += OMNI_GREEN + std::string("[") + GetDateWithMs() + std::string("] ") + OMNI_NONE;
            file_output += std::string("[") + GetDateWithMs() + std::string("] ");

            output += OMNI_LIGHT_BLUE + std::string("[") + position + std::string(":") + std::to_string(line) + std::string("] ") + OMNI_NONE;
            file_output += std::string("[") + position + std::string(":") + std::to_string(line) + std::string("] ");

            *stream_ << output;
            if (fstream_)
                *fstream_ << file_output;
            std::flush(*stream_);
            if (fstream_)
                std::flush(*fstream_);
        }
        virtual ~LockedStream() {
            std::flush(*stream_);
            if (fstream_)
                std::flush(*fstream_);
            lock_.unlock();
        }
        LockedStream(const LockedStream&) = delete;

        template <typename T>
        LockedStream& operator<<(const T& value) {
            *stream_ << value;
            if (fstream_)
                *fstream_ << value;
            return *this;
        }

        LockedStream& operator<<(std::ostream& (*manipulator)(std::ostream&)) {
            manipulator(*stream_);
            if (fstream_)
                manipulator(*fstream_);
            return *this;
        }
    
    private:
        std::ostream* stream_;
        std::ostream* fstream_;
        
        inline static std::mutex lock_;
    };

    class Logger {
    public:
        static inline void Initialize(std::ostream& stream, const std::string& filename) {
            stream_ = &stream;
            auto file = filename + "." + GetDate() + ".log";
            std::cout << file << std::endl;
            auto path = std::filesystem::path(file);
            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path());
            if (std::filesystem::exists(path))
                std::filesystem::remove(path);
            file_ = new std::ofstream(file, std::ios::out);
        }
        static inline void Initialize(std::ostream& stream) {
            stream_ = &stream;
            file_ = nullptr;
        }

        static inline
        LockedStream Log(LogLevel level, const char* tag = nullptr, const char* position = __FILE__, const int line = __LINE__) {
            if (!stream_)
                stream_ = &(std::cerr);
            return LockedStream(stream_, file_, level, tag, position, line);
        }

    private:
        inline static std::ostream *stream_;
        inline static std::ostream *file_;
    };
}

#undef OMNI_NONE         
#undef OMNI_RED         
#undef OMNI_LIGHT_RED   
#undef OMNI_GREEN      
#undef OMNI_LIGHT_GREEN  
#undef OMNI_BLUE      
#undef OMNI_LIGHT_BLUE  
#undef OMNI_DARY_GRAY    
#undef OMNI_CYAN         
#undef OMNI_LIGHT_CYAN  
#undef OMNI_PURPLE       
#undef OMNI_LIGHT_PURPLE 
#undef OMNI_BROWN        
#undef OMNI_YELLOW       
#undef OMNI_LIGHT_GRAY   
#undef OMNI_WHITE        

#endif
