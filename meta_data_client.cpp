
/* Opentracker */
#include "ot_metadata.hpp"

int main(int argc, char* argv[]){
	std::string magnet_uri = argv[1];
	std::string metadata = get_info_hash_metadata(magnet_uri);
	std::cout << "metadata: " << metadata << std::endl;
}