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

// word relationship to index in infohash_word_matrix;
std::map<std::string,int>  word_index_map;

// check if infohash is already been processed
std::map<std::string,int>  info_hash_index_map;

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
		if (metadata.size() > 0){
			std::cout << metadata[0] << std::endl;
			std::vector<std::string> left_hash_filt = split_string(magnet_uris[i], {"magnet:?xt=urn:btih:"});
			std::vector<std::string> right_hash_filt = split_string(left_hash_filt[left_hash_filt.size()-1], {"&tr="});
			std::string info_hash = right_hash_filt[0];
			std::cout << "info_hash: " << info_hash << std::endl;
			std::vector<std::string> filtered_metadata = split_string(metadata[0], {"->"});
			std::string torrent_metadata = filtered_metadata[filtered_metadata.size()-1];
			std::cout << "torrent_metadata:"  << torrent_metadata << std::endl;
			if (info_hash_index_map.count(info_hash) == 0){
				info_hash_index_map[info_hash] = 1;
				info_hash_vec.push_back(info_hash);
			
				parse_words_and_info_hash(
					torrent_metadata,
					word_infohash_indices_map, 
					info_hash_vec
				);
			}
			
		}
	}

	/* search index stuff below */

	// TODO: need ot check if matrix already exist and do append only, would need seperate normed matrix
	int info_hash_vec_size = info_hash_vec.size();
	std::cout << "info_hash_vec_size:" << info_hash_vec_size << std::endl; 
	infohash_word_matrix = construct_sparse_matrix(
		word_infohash_indices_map,
		info_hash_vec_size,
		word_index_map
	);

	row_normalize_matrix(infohash_word_matrix);
	
	partial_svd(infohash_word_matrix,
		u_mat,
		sigma_vec,
		v_matrix
	);
	/* search index stuff above */

	/* search low dimiensional space rep  below */
	start_right_hand_creation(
		u_mat,
		sigma_vec,
		v_matrix,
		isigma_ut, 
		isigma_vt
	);
	/* search low dimiensional space rep  above */

	/* search lookup below */
	std::string search_query = "Supernatural";
	std::cout << "post infohash_word_matrix.n_rows x infohash_word_matrix.n_cols: " << infohash_word_matrix.n_rows << " x " << infohash_word_matrix.n_cols << std::endl;
	std::cout << "Search query: " << "'"+search_query+"'" << std::endl;

	std::cout << "word_infohash_indices_map.size(): " << word_infohash_indices_map.size() << std::endl;
	
	std::cout << "word_index_map.size(): " << word_index_map.size() << std::endl;

	for (auto it: word_index_map)
		std::cout << "word: " << it.first << std::endl;
	
	std::vector<std::pair<int, double>> search_result = search_info_hash(
		search_query,
		word_index_map, 
		word_infohash_indices_map, 
		sigma_vec,
		isigma_ut, 
		infohash_word_matrix
	);

	if(search_result.size() > 0)
		std::cout << "\t\ttop info_hash index: " << search_result[0].first << " distance: " <<search_result[0].second << " search_result.size(): " << search_result.size() << std::endl;
	else
		std::cout << "\t\tNo docs found for " << search_query << std::endl;

	/* search lookup above */
}