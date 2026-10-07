#pragma once

#include <sn/sim/protocol.h>

#include <string>
#include <vector>

// Sign-ins (docs/adr/0016-sign-in-and-admin.md): a Login's proof that a
// provider (Steam, Google, Apple, Discord) knows the player, checked with
// that provider. The server asks a checker and polls for its answers, so
// that its tick never waits for a provider; stellanova-server's checker
// asks the providers (sn/server/providers.h), tests' answer at once.
namespace sn::server
{
using ph::u64;

// What a Login brought: sim::Login's provider, proof and nonce.
struct SignInProof
{
	std::string provider;
	std::string proof;
	std::string nonce;
};

// Who the provider says the player is, or why not (`error`: a strings key).
struct SignInAnswer
{
	std::string provider;
	std::string id;
	std::string name; // the provider's name for the player, when it gives one
	std::string error;
};

class SignInChecker
{
public:
	virtual ~SignInChecker() = default;
	// What clients are offered: the providers a browser can start.
	virtual std::vector<sim::SignInOffer> Offers() const = 0;
	// Starts checking; the answer comes through Poll with the same ticket.
	virtual void Check(u64 ticket, const SignInProof& proof) = 0;
	// The next answer, false when there is none yet.
	virtual bool Poll(u64& ticket, SignInAnswer& answer) = 0;
};
} // namespace sn::server
