/* Opentracker */
#include "ot_search.hpp"

//info_hash vector
std::vector<std::string> info_hash_vec;
// word -> string of index of info hashes
// if word is seen with info hash, append index of infohash from vec
std::map<std::string, std::string> word_infohash_indices_map;

// matrix where rows -> info_hash, colums -> words
arma::sp_dmat word_infohash_matrix;

arma::dmat u_mat;
arma::dvec sigma_vec;
arma::dmat v_matrix;

arma::dmat isigma_ut;
arma::dmat isigma_vt;

/* search lookup below */

/* search lookup above */

/* search low dimiensional space rep  below */


void start_right_hand_creation(arma::dmat u_mat, arma::dvec sigma_vec, arma::dmat v_matrix, arma::dmat& isigma_ut, arma::dmat& isigma_vt){
	arma::dmat m_sigma_i = arma::diagmat(sigma_vec);    
    
    for(int i=0; i< m_sigma_i.n_rows; ++i){
        double inverse_value = (1/m_sigma_i(i,i));
        m_sigma_i(i,i) = inverse_value;
    }

    arma::dmat m_u_t = u_mat;
    arma::inplace_trans(m_u_t);
    
    arma::dmat m_v_t = v_matrix;
    arma::inplace_trans(m_v_t);

    //multiply isigma_ut, m_u_t by inverse(m_sigma_i) to get m_u_t and m_v_t, respectivly
    isigma_ut = m_sigma_i * m_u_t;
    isigma_vt = m_sigma_i * m_v_t;
}

/* search low dimiensional space rep above */



/* search index stuff below */

std::string find_first_of(std::string input, std::vector<std::string> del){
    //get a map of delimeter and position of delimeter
    size_t pos;
    std::map<std::string, size_t> m;
    for (int i = 0; i < del.size(); i++){
        pos = input.find(del[i]);
        if (pos != std::string::npos)
            m[del[i]] = pos;
    }

    //find the smallest position of all delimeters i.e, find the smallest value in the map
    if (m.size() == 0)
        return "";
    size_t v = m.begin()->second;
    std::string k = m.begin()->first;

    for (auto it = m.begin(); it != m.end(); it++){
        if (it->second < v){
            v = it->second;
            k = it->first;
        }
    }
    return k;
}

std::vector<std::string> split_string(std::string input, std::vector<std::string> delimeters){
	// split string by ",", ".", " "
    std::vector<std::string> result;
    size_t pos = 0;
    std::string token;
    std::string delimeter = find_first_of(input, delimeters);

    while(delimeter != ""){
        if ((pos = input.find(delimeter)) != std::string::npos)
        {
            token = input.substr(0, pos);
            result.push_back(token);
            result.push_back(delimeter);
            input.erase(0, pos + delimeter.length());
        }
        delimeter = find_first_of(input, delimeters);
    }
    result.push_back(input);
    return result;
}

struct RetrieveKey{
    template <typename T> typename T::first_type operator()(T keyValuePair) const{
    	return keyValuePair.first;
    }
};

std::vector<std::string> extract_words_from_torrent_metadata(std::string &torrent_metadata){
	// get unique "words" from torrent metadata
	std::vector<std::string> words;
	std::map<std::string, int> words_map;
	std::transform(torrent_metadata.begin(), torrent_metadata.end(), torrent_metadata.begin(), [](unsigned char c) -> unsigned char { return std::tolower(c); });
	std::vector<std::string> words_vector = split_string(torrent_metadata, {",", ".", " "});
	for(int i=0;i<words_vector.size();i++){
		words_map[words_vector[i]] = 1;
	}
	std::transform(words_map.begin(), words_map.end(), std::back_inserter(words), RetrieveKey());
	return words;
}

void parse_words_and_info_hash(std::string& torrent_metadata, std::string& top_dir_path, std::map<std::string, std::string>& word_infohash_indices_map, std::vector<std::string>& info_hash_vec){
	std::cout << "	Parsing info hashes and Constructing Matrix" << std::endl;
	int start_vec_len = info_hash_vec.size();

	for(int i=0; i< start_vec_len;++i){
		std::vector<std::string> words = extract_words_from_torrent_metadata(torrent_metadata);
		if(words.size() > 0){
			for(int j=0; j< words.size(); ++j){
				if(word_infohash_indices_map.count(words[j]) > 0)
					word_infohash_indices_map[words[j]] += " " + std::to_string(i);
				else
					word_infohash_indices_map[words[j]] = std::to_string(i);
			}
		}
		if( i % 10000 == 0 && i != 0)
			std::cout << "		Hashes Parsed: " << i << std::endl;
	}
}

arma::sp_dmat construct_sparse_matrix(std::map<std::string, std::string>& word_infohash_indices_map, int& total_info_hashes, int& avg_words_per_info_hash){
	//(n_rows, n_cols) format
	int number_of_words = word_infohash_indices_map.size();
	arma::sp_dmat sparse_word_matrix(number_of_words, total_info_hashes);
	std::vector<std::string> word_vector; 
	std::transform(word_infohash_indices_map.begin(), word_infohash_indices_map.end(), std::back_inserter(word_vector), RetrieveKey());
	std::sort(word_vector.begin(), word_vector.end());
	std::cout << "	sparse matrix construction" << std::endl;
	for(int i=0;i<number_of_words;++i){
		std::vector<std::string> tmp_word_counts = split_string(word_infohash_indices_map[word_vector[i]], {" "});
		std::map<int, int> tmp_word_counts_map;
		std::vector<int> tmp_word_counts_keys;
		for(int j = 0; j<tmp_word_counts.size();++j){
			int column;
			std::stringstream(tmp_word_counts[j]) >> column;	
			if(tmp_word_counts_map.count(column) > 0)
				tmp_word_counts_map[column] += 1;
			else
				tmp_word_counts_map[column] = 1;
		}
		std::transform(tmp_word_counts_map.begin(), tmp_word_counts_map.end(), std::back_inserter(tmp_word_counts_keys), RetrieveKey());

		int tmp_word_counts_size = tmp_word_counts_keys.size();
		if(tmp_word_counts_size > 1){
			//std::cout << "i: " << i << " tmp_word_counts_size: " << tmp_word_counts_size << std::endl;
			for(int j=0; j<tmp_word_counts_size;++j){
				sparse_word_matrix(i,tmp_word_counts_keys[j]) += (float)tmp_word_counts_map[tmp_word_counts_keys[j]];
			}
		}

		if( i % 2000 == 0 && i != 0)
			std::cout << "		Word rows constructed: " << i << std::endl;
		
	}
	return sparse_word_matrix;
}

void row_normalize_matrix(arma::sp_dmat& word_infohash_matrix){
	std::cout << "	Matrix row normalization with 2-norm" << std::endl;
	word_infohash_matrix = arma::normalise(word_infohash_matrix, 2, 1);
}

void col_normalize_matrix(arma::sp_dmat& word_infohash_matrix){
	std::cout << "	Matrix col normalization with 2-norm" << std::endl;
	word_infohash_matrix = arma::normalise(word_infohash_matrix, 2, 0);
}

void partial_svd(arma::sp_dmat& word_infohash_matrix, arma::dmat& u_mat, arma::dvec& sigma_vec, arma::dmat& v_matrix){
	int dims = 200;
	bool svds_good = arma::svds(u_mat, sigma_vec, v_matrix, word_infohash_matrix, dims);
	if(!svds_good)
		std::cout << "		Partial decomp failed" << std::endl;
	else{
		std::cout << "		Partial decomp success" << std::endl;
	}
}
/* search index stuff above */