#include <locale>
#include <iostream>

int main(int argc, char* argv[]) {
	//std::locale iso8859_5(std::locale ("C"), new std::codecvt_byname<char,char,std::mbstate_t>("ru_RU.iso88595"));
	std::locale utf8("en_US.UTF-8");
	return 0;
}
