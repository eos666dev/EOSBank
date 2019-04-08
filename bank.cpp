#include "bank.hpp"

#define ACTION_CREATE_GAME 1
#define ACTION_ENTER_GAME  2

#define TAG_USER_COUNT     N(usercount)
#define TAG_PLAN_ID        N(planid)
#define TAG_DEPOSIT        N(deposit)
#define TAG_CLAIMED_PROFIT N(claimdprofit)
#define TAG_CLAIMED_REWARD N(claimdreward)
#define TAG_TEAM_COST      N(teamcost)

#define STATUS_PLAN_PREPARE 0
#define STATUS_PLAN_ACTIVED 1
#define STATUS_PLAN_END     2

using namespace eosio;
using namespace std;

class [[eosio::contract]] bank : public contract {
private:

    struct [[eosio::table]] st_plan {
        uint64_t  id;
        int64_t   day;

        // yiel / 1000
        int64_t   yield;

        name     player;
        asset    deposit;
        asset    claimed;
        uint64_t created_at;
        uint64_t updated_at;

        uint64_t unique;
        uint32_t status;

        uint64_t primary_key() const { return id; }
        uint64_t by_player() const { return player.value; }
        uint64_t by_unique() const { return unique; }
    };
    typedef multi_index<N(plan), st_plan,
        indexed_by<N(player), const_mem_fun<st_plan, uint64_t, &st_plan::by_player >>,
        indexed_by<N(unique), const_mem_fun<st_plan, uint64_t, &st_plan::by_unique >>
    > tb_plan;
    typedef multi_index<N(endplan), st_plan,
        indexed_by<N(player), const_mem_fun<st_plan, uint64_t, &st_plan::by_player >>,
        indexed_by<N(unique), const_mem_fun<st_plan, uint64_t, &st_plan::by_unique >>
    > tb_endplan;

    struct [[eosio::table]] st_global {
        name     key;
        uint64_t value;
        uint64_t primary_key() const { return key.value; }
    };
    typedef multi_index<N(global), st_global> tb_global;

    struct [[eosio::table]] st_user {
        name     player;
        name     referrer;

        asset    deposit;
        asset    profit;
        asset    reward;
        asset    claimed_reward;

        vector<uint64_t> sub;

        uint64_t primary_key() const { return player.value; }
        uint64_t by_referrer() const { return referrer.value; }
    };
    typedef multi_index<N(users), st_user,
        indexed_by<N(referrer), const_mem_fun<st_user, uint64_t, &st_user::by_referrer >>
    > tb_user;

#define plans_count 4

    st_plan plan_cfgs[plans_count] = {
        { .day = -1, .yield = 36 },
        { .day = 45, .yield = 46 },
        { .day = 25, .yield = 56 },
        { .day = 18, .yield = 66 },
    };

#define level_num 3

    // rate / 1000
    uint64_t ref_level[level_num] = {
        50, 20, 5
    };

    tb_user     _users;
    tb_plan     _plans;
    tb_endplan  _endplans;
    tb_global   _global;

    uint64_t DAY = 86400;

public:

    using contract::contract;
    bank(name receiver, name code, datastream<const char*> ds)
        : contract(receiver, code, ds),
        _global(receiver, receiver.value),
        _plans(receiver, receiver.value),
        _endplans(receiver, receiver.value),
        _users(receiver, receiver.value)
    {
    }

    [[eosio::action]]
    void claimprofit(name player, uint64_t pid) {
        require_auth(player);

        auto plan = _plans.find(pid);
        eosio_assert(plan != _plans.end(), "not found plan");
        eosio_assert(plan->player == player, "not your plan");
        eosio_assert(plan->status == STATUS_PLAN_ACTIVED, "plan not actived");

        uint64_t epoch = now();

        uint64_t end_at = plan->created_at + plan->day * DAY;
        bool endless = plan->day == -1;

        if (endless) {
            end_at = epoch + 1;
        }

        if (epoch >= end_at) {
            epoch = end_at;
        }

        int64_t p = plan->deposit.amount * (epoch - plan->created_at) * plan->yield / 1000 / DAY;
        asset profit = asset(p, EOS_SYMBOL);
        asset claim = profit - plan->claimed;

        eos_transfer(_self, player, claim, "profit claimed: #" + to_string(pid));
        add_global(TAG_CLAIMED_PROFIT, claim.amount);

        _plans.modify(plan, _self, [&](auto& p) {
            p.claimed    = profit;
            p.updated_at = now();
        });

        auto user = get_user(player, name{});
        _users.modify(user, _self, [&](auto& u) {
            u.profit += claim;
        });

        if (epoch < end_at) {
            return;
        }

        _endplans.emplace(_self, [&](auto& p) {
            p.id         = plan->id;
            p.day        = plan->day;
            p.yield      = plan->yield;
            p.player     = plan->player;
            p.deposit    = plan->deposit;
            p.claimed    = plan->claimed;
            p.unique     = plan->unique;
            p.status     = STATUS_PLAN_END;
            p.created_at = plan->created_at;
            p.updated_at = plan->updated_at;
        });

        _plans.erase(plan);
    }

    [[eosio::action]]
    void claimreward(name player) {
        require_auth(player);

        auto user = _users.find(player.value);
        eosio_assert(user != _users.end(), "no user");

        asset claim = user->reward - user->claimed_reward;

        eos_transfer(_self, player, claim, "reward claimed");
        add_global(TAG_CLAIMED_REWARD, claim.amount);

        _users.modify(user, _self, [&](auto& u) {
            u.claimed_reward = user->reward;
        });
    }

    [[eosio::action]]
    void prepare(name player, asset qty, uint64_t pid, uint64_t unique) {
        auto idx = _plans.get_index<N(unique)>();
        eosio_assert(idx.find(unique) == idx.end(), "already exist, try again");

        auto eidx = _endplans.get_index<N(unique)>();
        eosio_assert(eidx.find(unique) == eidx.end(), "already exist, try again");

        eosio_assert(pid >= 0 && pid < plans_count, "not support plan");
        eosio_assert(qty.amount >= 1000, "minimum deposit is 0.1 EOS");

        st_plan plan = plan_cfgs[pid];
        _plans.emplace(_self, [&](auto& p) {
            p.id         = next_plan_id();
            p.day        = plan.day;
            p.yield      = plan.yield;
            p.player     = player;
            p.deposit    = qty;
            p.claimed    = asset(0, EOS_SYMBOL);
            p.created_at = now();
            p.unique     = unique;
            p.status     = STATUS_PLAN_PREPARE;
        });
    }

    void transfer(name from, name to, asset qty, string memo) {
        require_auth(from);

        //eosio_assert(false, "not begin");

        eosio_assert(qty.amount >= 1000, "minimum deposit is 0.1 EOS");

        name ref;
        int pid;
        uint64_t unique;

        parse_memo(memo, &ref, &pid, &unique);
        eosio_assert(pid >= 0 && pid < plans_count, "not support plan");

        st_plan plan = plan_cfgs[pid];

        auto idx = _plans.get_index<N(unique)>();
        auto itr = idx.find(unique);
        eosio_assert(itr != idx.end(), "not prepare plan");

        eosio_assert(itr->status == STATUS_PLAN_PREPARE, "plan not prepared");

        eosio_assert(itr->day == plan.day, "plan day not match");
        eosio_assert(itr->yield == plan.yield, "plan yield not match");

        eosio_assert(itr->player == from, "plan player not match");
        eosio_assert(itr->deposit == qty, "plan deposit not match");

        idx.modify(itr, from, [&](auto& p) {
            p.day        = plan.day;
            p.yield      = plan.yield;
            p.player     = from;
            p.deposit    = qty;
            p.claimed    = asset(0, EOS_SYMBOL);
            p.unique     = unique;
            p.status     = STATUS_PLAN_ACTIVED;
            p.created_at = now();
            p.updated_at = now();
        });

        if (ref == from) {
            ref = name{};
        }

        auto user = get_user(from, ref);

        _users.modify(user, _self, [&](auto& u) {
            u.deposit += qty;
            if (u.referrer.value != 0) {
                u.reward  += qty / 200;
            }
        });

        reward_referrer(user->referrer, qty);

        add_global(TAG_DEPOSIT, qty.amount);

        qty = qty / 10;
        eos_transfer(_self, N(eosbankagent), qty, "agent cost");
        add_global(TAG_TEAM_COST, qty.amount);
    }

private:

    void parse_memo(string memo, name *referrer, int *plan, uint64_t *unique) {
        memo.erase(
            std::remove_if(
                memo.begin(), memo.end(),
                [](unsigned char x) {
                    return std::isspace(x);
                }),
            memo.end()
        );

        size_t sep_count = std::count(memo.begin(), memo.end(), '-');
        eosio_assert(sep_count == 2, "invalid memo");

        size_t pos;
        string container;
        pos = sub2sep(memo, &container, '-', 0, true);
        if (!container.empty()) {
            *referrer = name(container);
        }

        pos = sub2sep(memo, &container, '-', ++pos, true);
        *plan = atoi((char *) container.c_str());

        container = memo.substr(++pos);
        *unique = atoll((char *) container.c_str());
    }

    tb_user::const_iterator get_user(name user, name ref) {
        auto u = _users.find(user.value);
        if (u != _users.end()) {
            return u;
        }

        auto r = _users.find(ref.value);
        if (r == _users.end()) {
            ref = name{};
        }

        next_id(TAG_USER_COUNT);
        u = _users.emplace(_self, [&](auto &u) {
            u.player          = user;
            u.referrer        = ref;
            u.deposit         = asset(0, EOS_SYMBOL);
            u.profit          = asset(0, EOS_SYMBOL);
            u.reward          = asset(0, EOS_SYMBOL);
            u.claimed_reward  = asset(0, EOS_SYMBOL);
        });
        add_referrer(u->referrer);
        return u;
    }

    void add_referrer(name ref) {
        for (int i = 0; i < level_num; i++) {
            if (ref.value == 0) {
                return;
            }
            auto user = get_user(ref, name{});
            auto sub = user->sub;
            if (sub.size() < level_num) {
                sub.resize(level_num);
            }
            sub[i]++;
            _users.modify(user, _self, [&](auto& u) {
                u.sub = sub;
            });
            ref = user->referrer;
        }
    }

    void reward_referrer(name ref, asset qty) {
        for (int i = 0; i < level_num; i++) {
            if (ref.value == 0) {
                return;
            }
            auto user = get_user(ref, name{});
            _users.modify(user, _self, [&](auto& u) {
                u.reward += qty * ref_level[i] / 1000;
            });
            ref = user->referrer;
        }
    }

    uint64_t get_global(tb_global& g, name key) {
        auto itr = g.find(key.value);
        if (itr == g.end()) {
            return 0;
        }
        return itr->value;
    }

    void set_global(tb_global& g, name c, name key, uint64_t value) {
        auto itr = g.find(key.value);
        if (itr == g.end()) {
            itr = g.emplace(c, [&](auto& c) {c.key = key; c.value = 0;});
        }
        g.modify(itr, c, [&](auto& c) {c.value = value;});
    }

    uint64_t next_id(name tag) {
        uint64_t id = get_global(_global, tag);
        set_global(_global, _self, tag, ++id);
        return id;
    }

    uint64_t next_plan_id() {
        return next_id(TAG_PLAN_ID);
    }

    uint64_t add_global(name key, uint64_t n) {
        uint64_t d = get_global(_global, key) + n;
        set_global(_global, _self, key, d);
        return d;
    }
};

#ifdef EOSIO_DISPATCH
#undef EOSIO_DISPATCH
#endif
#define EOSIO_DISPATCH( TYPE, MEMBERS )                                    \
extern "C" {                                                               \
    void apply(uint64_t receiver, uint64_t code, uint64_t action) {        \
        if ( code == receiver ) {                                          \
            switch( action ) {                                             \
                EOSIO_DISPATCH_HELPER( TYPE, MEMBERS )                     \
            }                                                              \
        }                                                                  \
        if (code == N(eosio.token).value && action == N(transfer).value) { \
            execute_action(name(receiver), name(code), &bank::transfer);   \
            return;                                                        \
        }                                                                  \
        if (action == N(transfer).value) {                                 \
            eosio_assert(false, "only support EOS token");                 \
        }                                                                  \
    }                                                                      \
}

EOSIO_DISPATCH(bank, (claimprofit)(claimreward)(prepare))
