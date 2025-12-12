#include "Hash.h"

using std::cout;
using std::endl;
using std::string;

std::string Hash::sha256() const
{
	{
		unsigned char digest[SHA256_DIGEST_LENGTH];
		SHA256_CTX ctx;
		SHA256_Init(&ctx);
		SHA256_Update(&ctx, reinterpret_cast<const unsigned char *>(_data.data()),
					  _data.size());
		SHA256_Final(digest, &ctx);

		static const char *hex = "0123456789abcdef";
		std::string out;
		out.resize(SHA256_DIGEST_LENGTH * 2);
		for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i)
		{
			out[i * 2] = hex[(digest[i] >> 4) & 0xF];
			out[i * 2 + 1] = hex[digest[i] & 0xF];
		}
		return out;
	}
}

std::string Hash::hash_password(const string &salt)
{
	string input = _password + salt;
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256((const unsigned char *)input.c_str(), input.size(), hash);

	std::stringstream ss;
	for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
		ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
	return ss.str();
}