#include <random>

/* Opentracker */
#include "ot_metadata.hpp"
#include "ot_tracker_crawl.hpp"


int random(int min, int max) //range : [min, max]
{
   static bool first = true;
   if (first) 
   {  
      std::srand( time(NULL) ); //seeding for the first time only!
      first = false;
   }
   return min + std::rand() % (( max + 1 ) - min);
}

int main(int argc, char* argv[]){
	std::vector<std::string> trackers = get_tracker_urls();
	std::vector<std::string> magnet_uris;
	if (argc > 1){
		for (int i=1; i<argc; i++){
			std::string info_hash = argv[i];
			std::string magnet_uri = "magnet:?xt=urn:btih:" + info_hash + "&tr=http://tracker.opentrackr.org:1337/announce";//+ trackers[random(0, (int)trackers.size() - 1)];
			magnet_uris.push_back(magnet_uri);
			std::cout << "magnet_uri: " << magnet_uri << std::endl;
		}
	}
	std::vector<std::string> metadata = get_info_hash_metadata(magnet_uris);
	for (int i=0; i<magnet_uris.size(); i++){
		std::cout << metadata[i] << std::endl;		
	}
}