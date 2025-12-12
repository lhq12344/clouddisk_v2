#ifndef __WD_Hash_HPP__
#define __WD_Hash_HPP__

#include <iostream>
#include <openssl/sha.h>
#include <string>
#include <sstream>
#include <iomanip>
class Hash
{
public:
	Hash(const std::string &filename, const std::string &data)
		: _filename(filename),
		  _data(data),
		  _password() {}
	Hash(const std::string &password)
		: _filename(),
		  _data(),
		  _password(password) {}

	std::string sha256() const;
	std::string hash_password(const std::string &salt);

private:
	std::string _filename;
	std::string _data;
	std::string _password;
};

#endif
