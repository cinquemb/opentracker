#ifndef OT_SEARCH_H__
#define OT_SEARCH_H__

#include <algorithm>
#include <armadillo>
#include <cassert>


extern std::vector<std::string> split_string(std::string input, std::vector<std::string> delimeters);
/* search index stuff below */
extern void parse_words_and_info_hash(std::string& torrent_metadata, std::map<std::string, std::string>& word_infohash_indices_map, std::vector<std::string>& info_hash_vec);
extern arma::sp_dmat construct_sparse_matrix(std::map<std::string, std::string>& word_infohash_indices_map, int& total_info_hashes, std::map<std::string,int>&  word_index_map);
extern void row_normalize_matrix(arma::sp_dmat& infohash_word_matrix);
extern void partial_svd(arma::sp_dmat& infohash_word_matrix, arma::dmat& u_mat, arma::dvec& sigma_vec, arma::dmat& v_matrix);
/* search index stuff above */

/* search low dimiensional space rep  below */
extern void start_right_hand_creation(arma::dmat u_mat, arma::dvec sigma_vec, arma::dmat v_matrix, arma::dmat& isigma_ut, arma::dmat& isigma_vt);
/* search low dimiensional space rep  above */

/* search lookup below */
extern std::vector<std::pair<int, double>> search_info_hash(std::string& search_query, std::map<std::string,int>& word_index_map, std::map<std::string, std::string>& word_infohash_indices_map, arma::dvec& sigma_vec, arma::dmat& isigma_ut, arma::sp_dmat& infohash_word_matrix);
/* search lookup above */

#endif