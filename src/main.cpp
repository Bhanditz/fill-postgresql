// copyright defined in LICENSE.txt

#include "fill_postgresql_plugin.hpp"

#include <appbase/application.hpp>

#include <fc/exception/exception.hpp>
#include <fc/filesystem.hpp>
#include <fc/log/appender.hpp>
#include <fc/log/logger_config.hpp>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/exception/diagnostic_information.hpp>

using namespace appbase;

namespace fc {
std::unordered_map<std::string, appender::ptr>& get_appender_map();
}

namespace detail {

void configure_logging(const bfs::path& config_path) {
    try {
        try {
            fc::configure_logging(config_path);
        } catch (...) {
            elog("Error reloading logging.json");
            throw;
        }
    } catch (const fc::exception& e) {
        elog("${e}", ("e", e.to_detail_string()));
    } catch (const boost::exception& e) {
        elog("${e}", ("e", boost::diagnostic_information(e)));
    } catch (const std::exception& e) {
        elog("${e}", ("e", e.what()));
    } catch (...) {
        // empty
    }
}

} // namespace detail

void logging_conf_loop() {
    std::shared_ptr<boost::asio::signal_set> sighup_set(new boost::asio::signal_set(app().get_io_service(), SIGHUP));
    sighup_set->async_wait([sighup_set](const boost::system::error_code& err, int /*num*/) {
        if (!err) {
            ilog("Received HUP.  Reloading logging configuration.");
            auto config_path = app().get_logging_conf();
            if (fc::exists(config_path))
                ::detail::configure_logging(config_path);
            for (auto iter : fc::get_appender_map())
                iter.second->initialize(app().get_io_service());
            logging_conf_loop();
        }
    });
}

void initialize_logging() {
    auto config_path = app().get_logging_conf();
    if (fc::exists(config_path))
        fc::configure_logging(config_path); // intentionally allowing exceptions to escape
    for (auto iter : fc::get_appender_map())
        iter.second->initialize(app().get_io_service());

    logging_conf_loop();
}

enum return_codes {
    OTHER_FAIL      = -2,
    INITIALIZE_FAIL = -1,
    SUCCESS         = 0,
    BAD_ALLOC       = 1,
};

int main(int argc, char** argv) {
    try {
        auto root = fc::app_path();
        app().set_default_data_dir(root / "eosio/fill-postgresql/data");
        app().set_default_config_dir(root / "eosio/fill-postgresql/config");
        if (!app().initialize<fill_postgresql_plugin>(argc, argv))
            return INITIALIZE_FAIL;
        initialize_logging();
        ilog("fill-postgresql version ${ver}", ("ver", app().version_string()));
        ilog("fill-postgresql using configuration file ${c}", ("c", app().full_config_file_path().string()));
        ilog("fill-postgresql data directory is ${d}", ("d", app().data_dir().string()));
        app().startup();
        app().exec();
    } catch (const fc::exception& e) {
        elog("${e}", ("e", e.to_detail_string()));
        return OTHER_FAIL;
    } catch (const boost::interprocess::bad_alloc& e) {
        elog("bad alloc");
        return BAD_ALLOC;
    } catch (const boost::exception& e) {
        elog("${e}", ("e", boost::diagnostic_information(e)));
        return OTHER_FAIL;
    } catch (const std::runtime_error& e) {
        elog("${e}", ("e", e.what()));
        return OTHER_FAIL;
    } catch (const std::exception& e) {
        elog("${e}", ("e", e.what()));
        return OTHER_FAIL;
    } catch (...) {
        elog("unknown exception");
        return OTHER_FAIL;
    }

    return SUCCESS;
}
