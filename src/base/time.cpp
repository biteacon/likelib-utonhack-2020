#include "time.hpp"

namespace base
{

std::uint_least32_t Time::getSecondsInEpoch() const
{
    return _seconds_from_epoch_start;
}


std::chrono::time_point<std::chrono::system_clock> Time::toTimePoint() const
{
    std::chrono::duration<std::uint32_t, std::ratio<1, 1>> duration_from_epoch(_seconds_from_epoch_start);
    return std::chrono::time_point<std::chrono::system_clock>(duration_from_epoch);
}


bool Time::operator==(const Time& other) const
{
    return _seconds_from_epoch_start == other._seconds_from_epoch_start;
}


bool Time::operator!=(const Time& other) const
{
    return !(*this == other);
}


Time Time::now()
{
    Time time;
    time._seconds_from_epoch_start =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return time;
}


Time Time::fromSeconds(std::uint_least32_t seconds_from_epoch)
{
    Time time;
    time._seconds_from_epoch_start = seconds_from_epoch;
    return time;
}


Time Time::fromTimePoint(std::chrono::time_point<std::chrono::system_clock> time_point)
{
    Time time;
    time._seconds_from_epoch_start =
        std::chrono::duration_cast<std::chrono::seconds>(time_point.time_since_epoch()).count();
    return time;
}


::base::SerializationIArchive& operator>>(::base::SerializationIArchive& ia, Time& time)
{
    std::uint32_t timestamp;
    ia >> timestamp;
    time._seconds_from_epoch_start = static_cast<std::uint_least32_t>(timestamp);
    return ia;
}


::base::SerializationOArchive& operator<<(::base::SerializationOArchive& oa, const Time& time)
{
    oa << static_cast<std::uint32_t>(time._seconds_from_epoch_start);
    return oa;
}


} // namespace base