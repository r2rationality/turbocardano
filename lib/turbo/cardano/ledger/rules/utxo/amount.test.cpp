#include <turbo/cardano/ledger/rules/utxo/amount.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;
using cardano::ledger::rules::amount_sum;

suite ledger_amount_sum_suite = [] {
    "UTXO conservation does not wrap coin or asset totals"_test = [] {
        amount_sum outputs { UINT64_MAX };
        outputs += 2;
        expect(outputs != amount_sum { 1 });
        amount_sum inputs { UINT64_MAX - 1 };
        inputs += 3;
        expect(inputs == outputs); // valid aggregates above a single quantity's range
        inputs += outputs;
        expect(inputs.high == 2_u && inputs.low == 2_u);
    };
};
