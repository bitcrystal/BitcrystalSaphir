#include "base_security_neutral.h"
void get_encoded_table_my_neutral(char * encoded_table, unsigned int in_len)
{
	_srand(1427669233);
	for(int i = 0; i < in_len; i++)
	{
		encoded_table[i]=_rand(1,127);
	}
}
std::string encode_security_neutral(char const* bytes_to_encode, unsigned int in_len)
{
	std::string encode = base64_encode((unsigned char const*)&bytes_to_encode[0], in_len);
	unsigned int len = encode.length();
	char encoded_table[len];
	get_encoded_table_my_neutral(&encoded_table[0],len);
	for(int i = 0; i < len; i++)
	{	
		encode.at(i)=(int)encode.at(i)^(int)encoded_table[i];
	}
	return encode;
}
std::string decode_security_neutral(std::string & encoded_string)
{
	unsigned int len = encoded_string.length();
	char encoded_table[len];
	get_encoded_table_my_neutral(&encoded_table[0],len);
	for(int i = 0; i < len; i++)
	{	
		encoded_string.at(i)=(int)encoded_string.at(i)^(int)encoded_table[i];
	}
	std::string decode = base64_decode((std::string const)encoded_string);
	return decode;
}