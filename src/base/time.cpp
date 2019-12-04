#include "time.hpp"

namespace base
{

Time::Time(std::chrono::time_point<std::chrono::system_clock> time_point) : _time(time_point)
{}


std::uint_least32_t Time::millisecondsInEpoch() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(_time.time_since_epoch()).count();
}


std::uint_least32_t Time::secondsInEpoch() const
{
    return std::chrono::duration_cast<std::chrono::seconds>(_time.time_since_epoch()).count();
}

bool Time::operator==(const Time& other) const
{
    return _time == other._time;
}


bool Time::operator!=(const Time& other) const
{
    return !(*this == other);
}


Time Time::now()
{
    return Time(std::chrono::system_clock::now());
}


Time Time::fromMilliseconds(std::uint_least32_t milliseconds_from_epoch)
{
    std::chrono::duration<std::uint_least32_t, std::ratio<1, 1000>> duration_from_epoch(milliseconds_from_epoch);
    return Time(std::chrono::time_point<std::chrono::system_clock>(duration_from_epoch));
}


Time Time::fromSeconds(std::uint_least32_t seconds_from_epoch)
{
    std::chrono::duration<std::uint_least32_t, std::ratio<1, 1>> duration_from_epoch(seconds_from_epoch);
    return Time(std::chrono::time_point<std::chrono::system_clock>(duration_from_epoch));
}


::base::SerializationIArchive& operator>>(::base::SerializationIArchive& ia, Time& time)
{
    std::uint_least32_t timestamp;
    ia >> timestamp;
    time = base::Time::fromMilliseconds(timestamp);
    return ia;
}


::base::SerializationOArchive& operator<<(::base::SerializationOArchive& oa, const Time& time)
{
    return oa << time.millisecondsInEpoch();
}


} // namespace base
