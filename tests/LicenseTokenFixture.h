#pragma once
// Deterministic test-only Ed25519 seed. Never accepted by production defaults.
#include <openssl/evp.h>
#include <memory>
#include <string>
#include <string_view>
namespace license_fixture {
inline auto key(){std::string seed(32,'1');return std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)>(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519,nullptr,reinterpret_cast<const unsigned char*>(seed.data()),seed.size()),EVP_PKEY_free);}
inline std::string public_key(){auto k=key();std::string out(32,'\0');size_t n=32;EVP_PKEY_get_raw_public_key(k.get(),reinterpret_cast<unsigned char*>(out.data()),&n);return out;}
inline std::string base64(std::string_view data){std::string out((data.size()+2)/3*4+1,'\0');auto n=EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),reinterpret_cast<const unsigned char*>(data.data()),data.size());out.resize(n);for(auto&c:out){if(c=='+')c='-';if(c=='/')c='_';}while(out.ends_with('='))out.pop_back();return out;}
inline std::string sign(std::string_view payload){auto k=key();std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)>ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);EVP_DigestSignInit(ctx.get(),nullptr,nullptr,nullptr,k.get());std::string bytes=base64(R"({"alg":"EdDSA","kid":"fixture-owned-license","typ":"IISACC-ACCOUNT-LICENSES"})")+'.'+base64(payload);std::string sig(64,'\0');size_t n=64;EVP_DigestSign(ctx.get(),reinterpret_cast<unsigned char*>(sig.data()),&n,reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size());return bytes+'.'+base64(sig);}
}
