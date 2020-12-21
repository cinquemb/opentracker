/* Opentracker */
#include "ot_search.hpp"

template <class T1, class T2, class Pred = std::greater<T2>>
struct sort_pair_second {
    bool operator()(const std::pair<T1,T2>&left, const std::pair<T1,T2>&right) {
        Pred p;
        return p(left.second, right.second);
    }
};

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

std::vector<std::string> filter_words(std::vector<std::string>& temp_words){
	char chars[] = "()-.!'~\"><[] ";
	std::vector<std::string> words;
	for(int i=0;i<temp_words.size();++i){
		for(int j = 0; j < strlen(chars); ++j){
			temp_words[i].erase(std::remove(temp_words[i].begin(), temp_words[i].end(), chars[j]), temp_words[i].end());
		}
		if (temp_words[i].size() > 0)
			words.push_back(temp_words[i]);
	}
	return words;
}

/* search lookup below */
double compute_cosine_theta_distance(arma::dmat& search_query_low_dimensional_space_doc_vector, arma::dmat& temp_low_dimensional_space_doc_vector){
	double sum = 0.0;
    double a = 0.0;
    double b = 0.0;
	for(int i=0;i<temp_low_dimensional_space_doc_vector.n_rows;++i){
		sum += temp_low_dimensional_space_doc_vector(i,0) * search_query_low_dimensional_space_doc_vector(i,0);
        a += std::pow(temp_low_dimensional_space_doc_vector(i,0),2);
        b += std::pow(search_query_low_dimensional_space_doc_vector(i,0),2);
	}


    a = std::sqrt(a);
    b = std::sqrt(b);
	if(sum == 0){
        return 0;
    }
    else{
		double cos_theta = sum/(a*b);
		return cos_theta;
	}
}

// step 6
std::vector<std::pair<int, double>> search_info_hash(std::string& search_query, std::map<std::string,int>& word_index_map, std::map<std::string, std::string>& word_infohash_indices_map, arma::dvec& sigma_vec, arma::dmat& isigma_ut, arma::sp_dmat& infohash_word_matrix){
	int word_vector_size = word_infohash_indices_map.size();
	std::transform(search_query.begin(), search_query.end(), search_query.begin(), [](unsigned char c) -> unsigned char { return std::tolower(c); });
	std::vector<std::string> search_words = split_string(search_query, {" "});
	search_words = filter_words(search_words);
	std::map<int, std::string> seach_query_word_index_map;
	std::vector<std::pair<int, double> > doc_index_distance_map_vector;

	std::vector<std::string> word_vector; 
	std::transform(word_infohash_indices_map.begin(), word_infohash_indices_map.end(), std::back_inserter(word_vector), RetrieveKey());
	std::sort(word_vector.begin(), word_vector.end());
	
	
	for(int i=0; i< search_words.size(); ++i){
		if(word_infohash_indices_map.count(search_words[i]) > 0)
			seach_query_word_index_map[word_index_map[search_words[i]]] = search_words[i];
	}

	if(seach_query_word_index_map.size() == 0)
		return doc_index_distance_map_vector;

    arma::dmat search_doc_vector(word_vector_size,1);
    search_doc_vector.zeros();
    for(std::map<int,std::string>::iterator iter = seach_query_word_index_map.begin(); iter != seach_query_word_index_map.end(); ++iter){
        // std::cout << "search word row index:" << iter->first << std::endl;
        search_doc_vector(iter->first,0) = 1 / ((double)seach_query_word_index_map.size());
    }
    arma::dmat sigma = arma::diagmat(sigma_vec); 
    arma::dmat search_low_dimensional_space_doc_vector = isigma_ut * search_doc_vector;
    arma::dmat sigma_search_low_dimensional_space_doc_vector = sigma * search_low_dimensional_space_doc_vector;

    
	// iterate over rows that contain words
	//get column (document) indexes that contain search words
	std::vector<int> column_indecies;
	std::map<int, bool> col_seen;
	for(std::map<int,std::string>::iterator ie_iter = seach_query_word_index_map.begin(); ie_iter != seach_query_word_index_map.end(); ++ie_iter){
		

		arma::sp_dmat::const_row_iterator i = infohash_word_matrix.begin_row(ie_iter->first);
		arma::sp_dmat::const_row_iterator i_end = infohash_word_matrix.end_row(ie_iter->first);
		for(; i != i_end; ++i){
			if (col_seen.count(i.col()) == 0){
				column_indecies.push_back(i.col());
				col_seen[i.col()] = true;
			}
		}
	}

	// get all the words in a doc and compare
	for(int c=0; c < column_indecies.size(); ++c){
		arma::dmat temp_doc_col_vector(word_vector_size,1);
		temp_doc_col_vector.zeros();
		bool is_empty = true;

		arma::sp_dmat::const_col_iterator j = infohash_word_matrix.begin_col(column_indecies[c]);
		arma::sp_dmat::const_col_iterator j_end = infohash_word_matrix.end_col(column_indecies[c]);

		for(; j != j_end; ++j){
			is_empty = false;
			// std::cout << "c idx: in col (adding row): " << c << ": " << column_indecies[c] << " (" << j.row() <<"," << j.col() << ")" << std::endl;
			temp_doc_col_vector(j.row(),0) = (*j);
		}

		if(!is_empty){
			assert(isigma_ut.n_cols == word_vector_size);
			arma::dmat temp_low_dimensional_space_doc_vector = isigma_ut * temp_doc_col_vector;
            arma::dmat sigma_temp_low_dimensional_space_doc_vector = sigma * temp_low_dimensional_space_doc_vector;
			double distance = compute_cosine_theta_distance(sigma_search_low_dimensional_space_doc_vector, sigma_temp_low_dimensional_space_doc_vector);
			std::pair<int,double> tmp_pair = std::make_pair(column_indecies[c], distance);
			doc_index_distance_map_vector.push_back(tmp_pair);
		}
	}

	//std::cout << "total docs: " << doc_index_distance_map_vector.size() << std::endl;
	std::sort(doc_index_distance_map_vector.begin(), doc_index_distance_map_vector.end(), sort_pair_second<int,double>());
	return doc_index_distance_map_vector;
}

/* search lookup above */

/* search low dimiensional space rep  below */

// step 5
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
std::vector<std::string> extract_words_from_torrent_metadata(std::string &torrent_metadata){
	// get unique "words" from torrent metadata
	std::vector<std::string> words;
	std::map<std::string, int> words_map;
	std::transform(torrent_metadata.begin(), torrent_metadata.end(), torrent_metadata.begin(), [](unsigned char c) -> unsigned char { return std::tolower(c); });
	std::vector<std::string> words_vector = split_string(torrent_metadata, {",", ".", " ", "[", "]", "(", ")"});
	words_vector = filter_words(words_vector);
	for(int i=0;i<words_vector.size();i++){
		if(words_map.count(words_vector[i]) == 0){
			words_map[words_vector[i]] = 1;
			// std::cout << "words_vector[" << i << "]:" << words_vector[i] << std::endl;
		}
	}
	std::transform(words_map.begin(), words_map.end(), std::back_inserter(words), RetrieveKey());
	return words;
}

// step 1
void parse_words_and_info_hash(std::string& torrent_metadata, std::map<std::string, std::string>& word_infohash_indices_map, std::vector<std::string>& info_hash_vec){
	//std::cout << "	Parsing info hashes and Constructing Matrix" << std::endl;
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
		//if( i % 10000 == 0 && i != 0)
		//	std::cout << "		Hashes Parsed: " << i << std::endl;
	}
}

//step 2
arma::sp_dmat construct_sparse_matrix(std::map<std::string, std::string>& word_infohash_indices_map, int& total_info_hashes, std::map<std::string,int>&  word_index_map){
	//(n_rows [words], n_cols [documents]) format
	int number_of_words = word_infohash_indices_map.size();
	arma::sp_dmat sparse_word_matrix(number_of_words, total_info_hashes);
	std::vector<std::string> word_vector; 
	std::transform(word_infohash_indices_map.begin(), word_infohash_indices_map.end(), std::back_inserter(word_vector), RetrieveKey());
	std::sort(word_vector.begin(), word_vector.end());
	//std::cout << "	sparse matrix construction" << std::endl;
	std::map<std::string,int>  tmp_word_index_map;
	for(int i=0;i<number_of_words;++i){
		std::vector<std::string> tmp_word_counts = split_string(word_infohash_indices_map[word_vector[i]], {" "});
		tmp_word_index_map[word_vector[i]] = i;
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
		if(tmp_word_counts_size > 0){
			//std::cout << "i: " << i << " tmp_word_counts_size: " << tmp_word_counts_size << std::endl;
			for(int j=0; j<tmp_word_counts_size;++j){
				sparse_word_matrix(i,tmp_word_counts_keys[j]) += (float)tmp_word_counts_map[tmp_word_counts_keys[j]];
			}
		}

		//if( i % 2000 == 0 && i != 0)
		//	std::cout << "		Word rows constructed: " << i << std::endl;
		
	}
	word_index_map = tmp_word_index_map;
	return sparse_word_matrix;
}

//step 3
void row_normalize_matrix(arma::sp_dmat& infohash_word_matrix){
	//std::cout << "	Matrix row normalization with 2-norm" << std::endl;
	infohash_word_matrix = arma::normalise(infohash_word_matrix, 2, 1);
}

void col_normalize_matrix(arma::sp_dmat& infohash_word_matrix){
	//std::cout << "	Matrix col normalization with 2-norm" << std::endl;
	infohash_word_matrix = arma::normalise(infohash_word_matrix, 2, 0);
}

//step 4
void partial_svd(arma::sp_dmat& infohash_word_matrix, arma::dmat& u_mat, arma::dvec& sigma_vec, arma::dmat& v_matrix){
	int dims = 200;
	bool svds_good = arma::svds(u_mat, sigma_vec, v_matrix, infohash_word_matrix, dims);
	if(!svds_good)
		std::cout << "		Partial decomp failed" << std::endl;
}
/* search index stuff above */