#pragma once

#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/transaction.hpp>

using namespace eosio;
using namespace std;

#define N(X)        eosio::name{#X}
#define S(P,X)      eosio::symbol(#X,P)
#define EOS_SYMBOL  S(4, EOS)

#define N(X)   eosio::name{#X}
#define S(P,X) eosio::symbol(#X,P)

#define EBTC_SYMBOL    S(8, EBTC)
#define EETH_SYMBOL    S(8, EETH)
#define EUSD_SYMBOL    S(8, EUSD)

size_t sub2sep(
    const string& input,
    string* output,
    const char& separator,
    const size_t& first_pos = 0,
    const bool& required = false)
{
    eosio_assert(first_pos != string::npos, "invalid first pos");
    auto pos = input.find(separator, first_pos);
    if (pos == string::npos) {
        eosio_assert(!required, "parse memo error");
        return string::npos;
    }
    *output = input.substr(first_pos, pos - first_pos);
    return pos;
}


void token_transfer(name contract, name from, name to, asset qty, string memo) {
    if (!is_account(to)) {
        return;
    }
    if (qty.amount == 0) {
        return;
    }
    eosio_assert(qty.amount > 0, "invalid transfer");
    action(
        permission_level{from, N(active)},
        contract, N(transfer),
        make_tuple(from, to, qty, memo)
    ).send();
}

void eos_transfer(name from, name to, asset qty, string memo) {
    eosio_assert(qty.symbol == EOS_SYMBOL, "transfer eos symbol only");
    token_transfer(N(eosio.token), from, to, qty, memo);
}


struct [[eosio::table]] account {
    asset    balance;
    uint64_t primary_key()const { return balance.symbol.code().raw(); }
};
typedef eosio::multi_index<N(accounts), account > accounts;
asset token_available_balance(name c, symbol sym, name n) {
    accounts acnts = accounts(c, n.value);
    auto acnt = acnts.find(sym.code().raw() );
    if (acnt == acnts.end()) {
        return asset(0, sym);
    }
    return acnt->balance;
}

asset eos_available_balance(name n) {
    return token_available_balance(N(eosio.token), EOS_SYMBOL, n);
}
