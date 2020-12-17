#include <random>

/* Opentracker */
#include "ot_metadata.hpp"
#include "ot_tracker_crawl.hpp"
#include "ot_search.hpp"

//info_hash vector
std::vector<std::string> info_hash_vec;

// if word is seen with info hash, append index of infohash from vec
std::map<std::string, std::string> word_infohash_indices_map;

// matrix where rows -> words, colums -> info_hash
arma::sp_dmat infohash_word_matrix;

std::map<std::string,int>  word_index_map;

arma::dmat u_mat;
arma::dvec sigma_vec;
arma::dmat v_matrix;

arma::dmat isigma_ut;
arma::dmat isigma_vt;


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

std::string join_trackers(std::vector<std::string>& v, std::string delimiter){
	int vector_len = v.size();
	std::string ov;
	for(int i = 0; i< vector_len; i++){
		if(i < vector_len-1)
			 ov += v[i] + delimiter;
		else
			ov += v[i];
	}
	return ov;  
}

int main(int argc, char* argv[]){
	std::vector<std::string> trackers = get_tracker_urls();
	std::vector<std::string> magnet_uris;
	std::string tracker_urls = join_trackers(trackers, "&tr=");
	if (argc > 1){
		for (int i=1; i<argc; i++){
			std::string info_hash = argv[i];
			//std::string magnet_uri = "magnet:?xt=urn:btih:" + info_hash + "&tr=" + tracker_urls;//+ trackers[random(0, (int)trackers.size() - 1)];

			std::string magnet_uri = "magnet:?xt=urn:btih:" + info_hash + "&tr=http://tracker.opentrackr.org:1337/announce";//+ trackers[random(0, (int)trackers.size() - 1)];
			magnet_uris.push_back(magnet_uri);
			std::cout << "magnet_uri: " << magnet_uri << std::endl;
		}
	}
	//std::vector<std::string> metadata = get_info_hash_metadata(magnet_uris);
	for (int i=0; i<magnet_uris.size(); i++){
		std::vector<std::string> metadata = get_info_hash_metadata({magnet_uris[i]});
		if (metadata.size() > 0)
			std::cout << metadata[0] << std::endl;		
	}
}