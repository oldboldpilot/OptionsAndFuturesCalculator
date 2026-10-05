% sensen gateway -- the DEFAULT policy for the `sensen` org.
%
% @author Olumuyiwa Oluwasanmi
%
% "FOR NOW IT CAN SERVE WITH A DEFAULT POLICY WHICH IS NO POLICY FOR NOW -- or
% make the default policy be the local network/WAN/VPN connection." This is the
% second reading, deliberately, because the first one is not expressible: the
% prelude declares every authority predicate `sensen_policy_closed`, so a file
% that states nothing admits nothing. "No policy" would therefore be a gateway
% that refuses every request -- not an open one. The network reading is the
% honest version of the same intent: admit on the connection, and let the model
% grant and the placement carry the rest.
%
% WHAT THIS FILE ADDS, AND WHAT IT DELIBERATELY DOES NOT.
%
% `may_use_model/3` is the prelude's conjunction of THREE factors:
%
%     may_use_model(P, Model, M) :-
%         model_granted(P, Model), entitled_machine(P, M), hosts_allowed(Model, M).
%
% This file extends exactly ONE of them, `entitled_machine/2`, on the network
% the machine sits on. The other two are untouched and still have to hold:
%
%   * `model_granted/2` comes from the seed's `group_models` -- a principal
%     reaches only the models their group was granted, so adding a model to the
%     inventory does NOT publish it to everybody;
%   * `hosts_allowed/2` comes from the seed's `model_placements` -- a model is
%     served only where it was actually placed.
%
% So this is permissive on the CONNECTION axis and closed on the other two. It
% is not a hole: a principal with no group grant still gets nothing, which is
% what makes "no policy for now" safe to ship rather than merely convenient.
%
% `on_network/2` IS A REAL RELATION AND NOT A CONVENTION INVENTED HERE.
% `gateway_store::renderWorldFacts` emits one `on_network(Machine, Network)`
% per machine row from the seed's own `network` field, and closes the relation.
% The three atoms below are therefore matched against a fact the STORE states,
% so a typo here admits nothing rather than admitting everything -- the useful
% direction for a default-deny engine.
%
% ADDING A NETWORK IS AN EXPLICIT ACT. A machine seeded onto a network this
% file does not name is unreachable by every principal until a line is added,
% which is the "explicit policies, just like claude today" half of the request.

entitled_machine(P, M) :- principal(P), on_network(M, lan).
entitled_machine(P, M) :- principal(P), on_network(M, wan).
entitled_machine(P, M) :- principal(P), on_network(M, vpn).
