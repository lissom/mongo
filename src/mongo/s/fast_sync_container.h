/*
 * sharded_operation_executor.h
 *
 *  Created on: Jul 11, 2015
 *      Author: charlie
 */

#pragma

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "mongo/bson/bsonobj.h"

namespace mongo {

// TODO: Abstract this so depending on # of shards a simpler one can be used
/*
 * Holds results for a sharded operation
 * Each operation needs to know it's place apriori, no locking is used
 */
// TODO: Align the storage
template <typename ResultType>
class FastSyncContainer {
public:
    using ResultsContainer = std::vector<ResultType>;
    FastSyncContainer(size_t resultSize) : _resultsRemaining(resultSize),
            _results(resultSize) {
    }
    virtual ~FastSyncContainer();


    ResultsContainer* results() {
        fassert(-999481873, _resultsRemaining == 0);
        return &_results;
    }

    bool setResultsAndCheckComplete(ResultType&& results) {
        // If the results are all in the caller should complete work
        _results[--_containerCount] = std::move(results);
        size_t currentValue = _resultsRemaining;
        while (!_resultsRemaining.compare_exchange_weak(
            currentValue, currentValue - 1, std::memory_order_acq_rel))
            ;
        return currentValue == 0;

    }

private:
    std::atomic<size_t> _resultsRemaining;
    std::atomic<size_t> _containerCount;
    ResultsContainer _results;
};

using FastSyncBSONObj = FastSyncContainer<BSONObj>;
using FastSyncBSONObjPtr = std::unique_ptr<FastSyncBSONObj>;

} /* namespace mongo */
