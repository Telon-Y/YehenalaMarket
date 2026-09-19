#pragma once

#include "constants.h"

// Repeated Decimal normalization/subtraction can leave a reserved account a
// few trillionths below the quoted debit. Only forgive an absolute sub-cent
// arithmetic residue; real funding shortfalls still block settlement.
inline bool ConstructionFundsCover(Money balance, Money debit) {
    return isfinite(balance) && balance >= Money(0) &&
           (balance >= debit || debit - balance <= Money(1e-8));
}
