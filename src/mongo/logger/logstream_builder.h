/*    Copyright 2013 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#pragma once

#include <memory>
#include <sstream>
#include <string>

#include "mongo/base/object_function_signature.h"
#include "mongo/logger/labeled_level.h"
#include "mongo/logger/log_component.h"
#include "mongo/logger/log_severity.h"
#include "mongo/logger/message_log_domain.h"
#include "mongo/stdx/chrono.h"
#include "mongo/util/exit_code.h"

namespace mongo {
namespace logger {

    class Tee;

    /**
     * Stream-ish object used to build and append log messages.
     */
    class LogstreamBuilder {
    public:
        static LogSeverity severityCast(int ll) { return LogSeverity::cast(ll); }
        static LogSeverity severityCast(LogSeverity ls) { return ls; }
        static LabeledLevel severityCast(const LabeledLevel &labeled) { return labeled; }

        /**
         * Construct a LogstreamBuilder that writes to "domain" on destruction.
         *
         * "contextName" is a short name of the thread or other context.
         * "severity" is the logging severity of the message.
         */
        LogstreamBuilder(MessageLogDomain* domain,
                         std::string contextName,
                         LogSeverity severity);

        /**
         * Construct a LogstreamBuilder that writes to "domain" on destruction.
         *
         * "contextName" is a short name of the thread or other context.
         * "severity" is the logging severity of the message.
         * "component" is the primary log component of the message.
         */
        LogstreamBuilder(MessageLogDomain* domain,
                         std::string contextName,
                         LogSeverity severity,
                         LogComponent component);

        /**
         * Deprecated.
         */
        LogstreamBuilder(MessageLogDomain* domain,
                         const std::string& contextName,
                         LabeledLevel labeledLevel);

        /**
         * Move constructor.
         *
         * TODO: Replace with = default implementation when minimum MSVC version is bumped to
         * MSVC2015.
         */
        LogstreamBuilder(LogstreamBuilder&& other);

        /**
         * Move assignment operator.
         *
         * TODO: Replace with =default implementation when minimum MSVC version is bumped to VS2015.
         */
        LogstreamBuilder& operator=(LogstreamBuilder&& other);

        /**
         * Destroys a LogstreamBuilder().  If anything was written to it via stream() or operator<<,
         * constructs a MessageLogDomain::Event and appends it to the associated domain.
         */
        ~LogstreamBuilder();


        /**
         * Sets an optional prefix for the message.
         */
        LogstreamBuilder& setBaseMessage(const std::string& baseMessage) {
            _baseMessage = baseMessage;
            return *this;
        }

        std::ostream& stream() { if (!_os) makeStream(); return *_os; }

        OBJECT_HAS_FUNCTION_SIGNATURE(HasToString, toString, void, void)

        template<typename T, bool hasStreamOperator>
        struct StreamVariable { };

        template<typename T>
        struct StreamVariable<T, false> {
            static LogstreamBuilder& print(LogstreamBuilder& stream, const T& t) {
                stream << t;
                return stream;
            }
        };

        template<typename T>
        struct StreamVariable<T, true> {
            static LogstreamBuilder& print(LogstreamBuilder& stream, const T& t) {
                stream << t.toString();
                return stream;
            }
        };

        template <typename T>
        LogstreamBuilder& operator<<(const T& x) {
            StreamVariable<T, HasToString<T>::value>::print(*this, x);
            return *this;
        }

        LogstreamBuilder& operator<< (std::ostream& ( *manip )(std::ostream&)) {
            stream() << manip;
            return *this;
        }
        LogstreamBuilder& operator<< (std::ios_base& (*manip)(std::ios_base&)) {
            stream() << manip;
            return *this;
        }

        /**
         * In addition to appending the message to _domain, write it to the given tee.  May only
         * be called once per instance of LogstreamBuilder.
         */
        void operator<<(Tee* tee);

    private:

        void makeStream();

        MessageLogDomain* _domain;
        std::string _contextName;
        LogSeverity _severity;
        LogComponent _component;
        std::string _baseMessage;
        std::unique_ptr<std::ostringstream> _os;
        Tee* _tee;

    };


}  // namespace logger
}  // namespace mongo
