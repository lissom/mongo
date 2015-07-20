/*
 * async_state.h
 *
 *  Created on: Jul 12, 2015
 *      Author: charlie
 */

#pragma once

namespace mongo {

class AsyncState {
public:
    /*
         * kInit - starting
         * kRunning - processing outgoing requests
         * kWait - waiting for io requests to complete
         * kError - A fatal event took place, the operation should end as soon as possible
         * kResultsReady - Ready to send, but we may want to keep the buffer around for sending, etc
         *                 This state is *not* good in so far as it's read only essentially, other
         *                 resources may be freed at this point, safe to read, move into or delete
         * kComplete - all operations are complete
         *
         * kError should be treated as a modified kWait where operations can be canceled and results
         * can be thrown away.  The operation is still running, so it shouldn't be destroyed
         */
        enum class State {
            kInit, kRunning, kWait, kError, kResultsReady, kComplete
        };

        State state() const { return _state; }

        bool active() {
            return _state != State::kComplete && _state != State::kInit;
        }

        bool resultsAvailable() { return _state == State::kResultsReady
                || _state == State::kComplete; }

        void setState(State newState) {
            State currentState = _state.load(std::memory_order_consume);
            switch (newState) {
            case State::kError :
                //no break
            case State::kResultsReady :
                fassert(-66611, currentState != State::kResultsReady);
                //no break
            case State::kComplete :
                do {
                    fassert(-66612, currentState != State::kComplete);
                } while (!_state.compare_exchange_weak(currentState, newState, std::memory_order_acquire));
                break;
            default :
                do {
                    fassert(-66613, currentState != State::kComplete);
                    if (currentState != State::kError)
                        break;
                } while (!_state.compare_exchange_weak(currentState, newState, std::memory_order_acquire));

            }

        }

        operator State() { return _state; }

private:
        std::atomic<State> _state { State::kInit };
};

} /* namespace mongo */
