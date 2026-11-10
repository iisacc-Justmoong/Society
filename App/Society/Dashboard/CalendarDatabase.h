#pragma once
#include <iiFileProviderStorage.h>
#include <sqlite3.h>
#include <memory>
#include <stdexcept>
#include <string>

// The application owns the on-disk connection; iiCalendar only consumes it.
inline std::shared_ptr<sqlite3> openSocietyCalendarDatabase(const std::string& path) {
    sqlite3* raw=nullptr;
    const int status=iiFileProvider::native::openDatabase(path.c_str(),&raw,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX);
    if(status!=SQLITE_OK) {
        const std::string message=raw?sqlite3_errmsg(raw):"Calendar database could not be opened";
        if(raw)sqlite3_close_v2(raw);
        throw std::runtime_error(message);
    }
    return {raw,[](sqlite3* db){if(db)sqlite3_close_v2(db);}};
}
