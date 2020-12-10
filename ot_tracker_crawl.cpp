/* Opentracker */
#include "ot_tracker_crawl.hpp"

static size_t write_callback(void *contents, size_t size, size_t nmeb, void *userp){
	((std::string*)userp)->append((char*)contents, size * nmeb);
	return size * nmeb;
}

std::string get_data_from_url(std::string& url, std::vector<std::string>& header_strings){
	CURL *curl;
	CURLcode res;
	struct curl_slist *headers = NULL;
	for (int i=0; i<header_strings.size();i++)
		headers = curl_slist_append(headers, header_strings[i].c_str());
	std::string read_buffer;

	curl = curl_easy_init();
	if(curl){
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
		//curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5);
		//curl_easy_setopt(curl, CURLOPT_PROXY, proxy_addr.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
		if (header_strings.size() > 0){
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		}
		res = curl_easy_perform(curl);
		/*
		if (res != CURLE_OK)
			std::cout << "fail" << std::endl;
		*/

		if (header_strings.size() > 0)
			curl_slist_free_all(headers);

		curl_easy_cleanup(curl);
	}

	return read_buffer;
}