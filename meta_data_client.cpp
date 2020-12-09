#include <iterator>

#include <libtorrent/config.hpp>

#include <boost/unordered_set.hpp>

#include <libtorrent/extensions/metadata_transfer.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
#include <libtorrent/extensions/smart_ban.hpp>


#include <libtorrent/bencode.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/time.hpp>
#include <libtorrent/magnet_uri.hpp>

#include <curl/curl.h>

#include <armadillo>

typedef std::multimap<std::string, libtorrent::torrent_handle> handles_t;

FILE* g_log_file = 0;
int active_torrent = 0;
enum {
	torrents_all,
	torrents_downloading,
	torrents_not_paused,
	torrents_seeding,
	torrents_queued,
	torrents_stopped,
	torrents_checking,
	torrents_feeds,

	torrents_max
};

int torrent_filter = torrents_not_paused;

std::string monitor_dir;
std::string outgoing_interface = "";
int max_connections_per_torrent = 50;
int torrent_upload_limit = 0;
int torrent_download_limit = 0;
// if non-empty, a peer that will be added to all torrents
std::string peer;

// the number of times we've asked to save resume data
// without having received a response (successful or failure)
int num_outstanding_resume_data = 0;

int hex_to_int(char in){
	if (in >= '0' && in <= '9') return int(in) - '0';
	if (in >= 'A' && in <= 'F') return int(in) - 'A' + 10;
	if (in >= 'a' && in <= 'f') return int(in) - 'a' + 10;
	return -1;
}

std::string path_append(std::string const& lhs, std::string const& rhs)
{
	if (lhs.empty() || lhs == ".") return rhs;
	if (rhs.empty() || rhs == ".") return lhs;
#define TORRENT_SEPARATOR "/"
	bool need_sep = lhs[lhs.size()-1] != '/';
	return lhs + (need_sep?TORRENT_SEPARATOR:"") + rhs;
}

int load_file(std::string const& filename, std::vector<char>& v, libtorrent::error_code& ec, int limit = 8000000){
	ec.clear();
	FILE* f = fopen(filename.c_str(), "rb");
	if (f == NULL)
	{
		if(f == NULL){
			ec.assign(errno, boost::system::get_generic_category());
			std::cout << "fail file create: " << filename << std::endl;
			return -1;
		}
	}

	int r = fseek(f, 0, SEEK_END);
	if (r != 0)
	{
		ec.assign(errno, boost::system::get_generic_category());
		fclose(f);
		std::cout << "fail file seek" << std::endl;
		return -1;
	}
	long s = ftell(f);
	if (s < 0)
	{
		ec.assign(errno, boost::system::get_generic_category());
		fclose(f);
		std::cout << "fail file tell" << std::endl;
		return -1;
	}

	if (s > limit)
	{
		fclose(f);
		return -2;
	}

	r = fseek(f, 0, SEEK_SET);
	if (r != 0)
	{
		ec.assign(errno, boost::system::get_generic_category());
		fclose(f);
		std::cout << "fail file set" << std::endl;
		return -1;
	}

	v.resize(s);
	if (s == 0)
	{
		fclose(f);
		return 0;
	}

	r = fread(&v[0], 1, v.size(), f);
	if (r < 0)
	{
		ec.assign(errno, boost::system::get_generic_category());
		fclose(f);
		std::cout << "fail file read" << std::endl;
		return -1;
	}

	fclose(f);

	if (r != s) return -3;

	return 0;
}

libtorrent::torrent_status const& get_active_torrent(std::vector<libtorrent::torrent_status const*> const& filtered_handles)
{
	if (active_torrent >= int(filtered_handles.size())
		|| active_torrent < 0) active_torrent = 0;
	return *filtered_handles[active_torrent];
}

bool show_torrent(libtorrent::torrent_status const& st, int torrent_filter, int* counters)
{
	++counters[torrents_all];
	
	if (!st.paused
		&& st.state != libtorrent::torrent_status::seeding
		&& st.state != libtorrent::torrent_status::finished)
	{
		++counters[torrents_downloading];
	}

	if (!st.paused) ++counters[torrents_not_paused];

	if (!st.paused
		&& (st.state == libtorrent::torrent_status::seeding
		|| st.state == libtorrent::torrent_status::finished))
	{
		++counters[torrents_seeding];
	}

	if (st.paused && st.auto_managed)
	{
		++counters[torrents_queued];
	}

	if (st.paused && !st.auto_managed)
	{
		++counters[torrents_stopped];
	}

	if (st.state == libtorrent::torrent_status::checking_files
		|| st.state == libtorrent::torrent_status::queued_for_checking)
	{
		++counters[torrents_checking];
	}

	switch (torrent_filter)
	{
		case torrents_all: return true;
		case torrents_downloading:
			return !st.paused
			&& st.state != libtorrent::torrent_status::seeding
			&& st.state != libtorrent::torrent_status::finished;
		case torrents_not_paused: return !st.paused;
		case torrents_seeding:
			return !st.paused
			&& (st.state == libtorrent::torrent_status::seeding
			|| st.state == libtorrent::torrent_status::finished);
		case torrents_queued: return st.paused && st.auto_managed;
		case torrents_stopped: return st.paused && !st.auto_managed;
		case torrents_checking: return st.state == libtorrent::torrent_status::checking_files
			|| st.state == libtorrent::torrent_status::queued_for_checking;
		case torrents_feeds: return false;
	}
	return true;
}

bool compare_torrent(libtorrent::torrent_status const* lhs, libtorrent::torrent_status const* rhs)
{
	if (lhs->queue_position != -1 && rhs->queue_position != -1)
	{
		// both are downloading, sort by queue pos
		return lhs->queue_position < rhs->queue_position;
	}
	else if (lhs->queue_position == -1 && rhs->queue_position == -1)
	{
		// both are seeding, sort by seed-rank
		if (lhs->seed_rank != rhs->seed_rank)
			return lhs->seed_rank > rhs->seed_rank;

		return lhs->info_hash < rhs->info_hash;
	}

	return (lhs->queue_position == -1) < (rhs->queue_position == -1);
}

void update_filtered_torrents(boost::unordered_set<libtorrent::torrent_status>& all_handles
	, std::vector<libtorrent::torrent_status const*>& filtered_handles, int* counters)
{
	filtered_handles.clear();
	memset(counters, 0, sizeof(int) * torrents_max);
	for (boost::unordered_set<libtorrent::torrent_status>::iterator i = all_handles.begin()
		, end(all_handles.end()); i != end; ++i)
	{
		if (!show_torrent(*i, torrent_filter, counters)) continue;
		filtered_handles.push_back(&*i);
	}
	if (active_torrent >= int(filtered_handles.size())) active_torrent = filtered_handles.size() - 1;
	else if (active_torrent == -1 && !filtered_handles.empty()) active_torrent = 0;
	std::sort(filtered_handles.begin(), filtered_handles.end(), &compare_torrent);
}

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

char const* timestamp()
{
	time_t t = std::time(0);
	tm* timeinfo = std::localtime(&t);
	static char str[200];
	std::strftime(str, 200, "%b %d %X", timeinfo);
	return str;
}

char const* esc(char const* code)
{
#ifdef ANSI_TERMINAL_COLORS
	// this is a silly optimization
	// to avoid copying of strings
	enum { num_strings = 200 };
	static char buf[num_strings][20];
	static int round_robin = 0;
	char* ret = buf[round_robin];
	++round_robin;
	if (round_robin >= num_strings) round_robin = 0;
	ret[0] = '\033';
	ret[1] = '[';
	int i = 2;
	int j = 0;
	while (code[j]) ret[i++] = code[j++];
	ret[i++] = 'm';
	ret[i++] = 0;
	return ret;
#else
	return "";
#endif
}

void print_alert(libtorrent::alert const* a, std::string& str)
{
#ifdef ANSI_TERMINAL_COLORS
	if (a->category() & libtorrent::alert::error_notification)
	{
		str += esc("31");
	}
	else if (a->category() & (libtorrent::alert::peer_notification | libtorrent::alert::storage_notification))
	{
		str += esc("33");
	}
#endif
	str += "[";
	str += timestamp();
	str += "] ";
	str += a->message();
#ifdef ANSI_TERMINAL_COLORS
	str += esc("0");
#endif

	if (g_log_file)
		fprintf(g_log_file, "[%s] %s\n", timestamp(),  a->message().c_str());

	printf("[%s] %s\n", timestamp(), a->message().c_str());
}

int save_file(std::string const& filename, std::vector<char>& v)
{
	FILE* f = fopen(filename.c_str(), "wb");
	if (f == NULL)
		return -1;

	int w = fwrite(&v[0], 1, v.size(), f);
	if (w < 0)
	{
		fclose(f);
		return -1;
	}

	if (w != int(v.size())) return -3;
	fclose(f);
	return 0;
}

// returns true if the alert was handled (and should not be printed to the log)
// returns false if the alert was not handled
bool handle_alert(libtorrent::session& ses, libtorrent::alert* a
	, handles_t& files, std::set<libtorrent::torrent_handle>& non_files
	, int* counters, boost::unordered_set<libtorrent::torrent_status>& all_handles
	, std::vector<libtorrent::torrent_status const*>& filtered_handles
	, bool& need_resort)
{
#ifdef TORRENT_USE_OPENSSL
	if (libtorrent::torrent_need_cert_alert* p = libtorrent::alert_cast<libtorrent::torrent_need_cert_alert>(a))
	{
		libtorrent::torrent_handle h = p->handle;
		std::string cert = path_append("certificates", libtorrent::to_hex(h.info_hash().to_string())) + ".pem";
		std::string priv = path_append("certificates", libtorrent::to_hex(h.info_hash().to_string())) + "_key.pem";

		struct ::stat st;
		int ret = ::stat(cert.c_str(), &st);
		if (ret < 0 || (st.st_mode & S_IFREG) == 0)
		{
			char msg[256];
			snprintf(msg, sizeof(msg), "ERROR. could not load certificate %s: %s\n", cert.c_str(), strerror(errno));
			if (g_log_file) fprintf(g_log_file, "[%s] %s\n", timestamp(), msg);
			std::cout << "ERROR. could not load certificate " << std::endl;
			return true;
		}

		ret = ::stat(priv.c_str(), &st);
		if (ret < 0 || (st.st_mode & S_IFREG) == 0)
		{
			char msg[256];
			snprintf(msg, sizeof(msg), "ERROR. could not load private key %s: %s\n", priv.c_str(), strerror(errno));
			if (g_log_file) fprintf(g_log_file, "[%s] %s\n", timestamp(), msg);
			std::cout << "ERROR. could not load private key" << std::endl;
			return true;
		}

		char msg[256];
		snprintf(msg, sizeof(msg), "loaded certificate %s and key %s\n", cert.c_str(), priv.c_str());
		if (g_log_file) fprintf(g_log_file, "[%s] %s\n", timestamp(), msg);

		h.set_ssl_certificate(cert, priv, "certificates/dhparams.pem", "1234");
		std::cout << "using ssl" << std::endl;
		h.resume();
	}
#endif

	boost::intrusive_ptr<libtorrent::torrent_info const> ti;

	if (libtorrent::metadata_received_alert* p = libtorrent::alert_cast<libtorrent::metadata_received_alert>(a))
	{
		// if we have a monitor dir, save the .torrent file we just received in it
		// also, add it to the files map, and remove it from the non_files list
		// to keep the scan dir logic in sync so it's not removed, or added twice
		libtorrent::torrent_handle h = p->handle;
		std::cout << "received alert" << std::endl;
		if (h.is_valid()) {
			if (!ti) ti = h.torrent_file();
			libtorrent::create_torrent ct(*ti);
			libtorrent::entry te = ct.generate();
			std::vector<char> buffer;
			bencode(std::back_inserter(buffer), te);
			std::string filename = ti->name() + "." + libtorrent::to_hex(ti->info_hash().to_string()) + ".torrent";

			std::string file_comment = ti->comment();
			std::cout << "name:" << ti->name() << std::endl;
			std::cout << "\t file_comment: " << file_comment << std::endl;
			exit(0);

			filename = path_append(monitor_dir, filename);
			save_file(filename, buffer);

			files.insert(std::pair<std::string, libtorrent::torrent_handle>(filename, h));
			non_files.erase(h);
		}
	}
	else if (libtorrent::add_torrent_alert* p = libtorrent::alert_cast<libtorrent::add_torrent_alert>(a))
	{
		std::string filename;
		if (p->params.userdata)
		{
			filename = (char*)p->params.userdata;
			free(p->params.userdata);
		}

		if (p->error)
		{
			fprintf(stderr, "failed to add torrent: %s %s\n", filename.c_str(), p->error.message().c_str());
			std::cout << "failed to add torrent" << std::endl;
		}
		else
		{
			libtorrent::torrent_handle h = p->handle;
			if (!filename.empty())
				files.insert(std::pair<const std::string, libtorrent::torrent_handle>(filename, h));
			else
				non_files.insert(h);

			h.set_max_connections(max_connections_per_torrent);
			h.set_max_uploads(-1);
			h.set_upload_limit(torrent_upload_limit);
			h.set_download_limit(torrent_download_limit);
			h.use_interface(outgoing_interface.c_str());

			// if we have a peer specified, connect to it
			if (!peer.empty())
			{
				char* port = (char*) strrchr((char*)peer.c_str(), ':');
				if (port >  (void *)0)
				{
					*port++ = 0;
					char const* ip = peer.c_str();
					int peer_port = atoi(port);
					libtorrent::error_code ec;
					if (peer_port > 0)
						h.connect_peer(libtorrent::tcp::endpoint(libtorrent::address::from_string(ip, ec), peer_port));
				}
			}

			boost::unordered_set<libtorrent::torrent_status>::iterator j
				= all_handles.insert(h.status()).first;
			if (show_torrent(*j, torrent_filter, counters))
			{
				filtered_handles.push_back(&*j);
				need_resort = true;
			}
		}
	}
	else if (libtorrent::torrent_finished_alert* p = libtorrent::alert_cast<libtorrent::torrent_finished_alert>(a))
	{
		p->handle.set_max_connections(max_connections_per_torrent / 2);

		std::cout << "torrent_finished_alert" << std::endl;

		// write resume data for the finished torrent
		// the alert handler for save_resume_data_alert
		// will save it to disk
		libtorrent::torrent_handle h = p->handle;
		h.save_resume_data();
		++num_outstanding_resume_data;
	}
	else if (libtorrent::save_resume_data_alert* p = libtorrent::alert_cast<libtorrent::save_resume_data_alert>(a))
	{
		--num_outstanding_resume_data;
		libtorrent::torrent_handle h = p->handle;
		TORRENT_ASSERT(p->resume_data);
		if (p->resume_data)
		{
			std::vector<char> out;
			bencode(std::back_inserter(out), *p->resume_data);
			libtorrent::torrent_status st = h.status(libtorrent::torrent_handle::query_save_path);
			save_file(path_append(st.save_path, path_append(".resume", libtorrent::to_hex(st.info_hash.to_string()) + ".resume")), out);
			if (h.is_valid()
				&& non_files.find(h) == non_files.end()
				&& std::find_if(files.begin(), files.end()
					, boost::bind(&handles_t::value_type::second, _1) == h) == files.end())
				ses.remove_torrent(h);

			std::cout << "save_resume_data_alert" << std::endl;
		}
	}
	else if (libtorrent::save_resume_data_failed_alert* p = libtorrent::alert_cast<libtorrent::save_resume_data_failed_alert>(a))
	{
		--num_outstanding_resume_data;
		libtorrent::torrent_handle h = p->handle;
		std::cout << "save_resume_data_failed_alert" << std::endl;
		if (h.is_valid()
			&& non_files.find(h) == non_files.end()
			&& std::find_if(files.begin(), files.end()
				, boost::bind(&handles_t::value_type::second, _1) == h) == files.end()){

			std::string file_comment = h.torrent_file()->comment();
			std::cout << "\t file_comment: " << file_comment << std::endl;
			ses.remove_torrent(h);
		}
	}
	else if (libtorrent::torrent_paused_alert* p = libtorrent::alert_cast<libtorrent::torrent_paused_alert>(a))
	{
		// write resume data for the finished torrent
		// the alert handler for save_resume_data_alert
		// will save it to disk
		libtorrent::torrent_handle h = p->handle;
		h.save_resume_data();
		++num_outstanding_resume_data;
		std::cout << "torrent_paused_alert" << std::endl;
	}
	else if (libtorrent::state_update_alert* p = libtorrent::alert_cast<libtorrent::state_update_alert>(a))
	{
		bool need_filter_update = false;
		for (std::vector<libtorrent::torrent_status>::iterator i = p->status.begin();
			i != p->status.end(); ++i)
		{
			boost::unordered_set<libtorrent::torrent_status>::iterator j = all_handles.find(*i);
			// don't add new entries here, that's done in the handler
			// for add_torrent_alert
			if (j == all_handles.end()) continue;
			if (j->state != i->state
				|| j->paused != i->paused
				|| j->auto_managed != i->auto_managed)
				need_filter_update = true;
			((libtorrent::torrent_status&)*j) = *i;
		}
		std::cout << "state_update_alert" << std::endl;
		if (need_filter_update)
			update_filtered_torrents(all_handles, filtered_handles, counters);

		return true;
	}
	return false;
}

int main(int argc, char* argv[])
{

	libtorrent::session_settings settings;
	libtorrent::proxy_settings ps;
	std::deque<std::string> events;

	// the string is the filename of the .torrent file, but only if
	// it was added through the directory monitor. It is used to
	// be able to remove torrents that were added via the directory
	// monitor when they're not in the directory anymore.
	boost::unordered_set<libtorrent::torrent_status> all_handles;
	std::vector<libtorrent::torrent_status const*> filtered_handles;
	// maps filenames to torrent_handles
	handles_t files;
	// torrents that were not added via the monitor dir
	std::set<libtorrent::torrent_handle> non_files;
	bool start_dht = true;
	bool start_upnp = true;
	bool start_lsd = true;
	int loop_limit = 0;	
	int counters[torrents_max];
	memset(counters, 0, sizeof(counters));

	libtorrent::error_code ec;
	std::vector<char> in;

	libtorrent::session ses(libtorrent::fingerprint("LT", LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR, 0, 0)
		, libtorrent::session::add_default_plugins
		, libtorrent::alert::all_categories
			& ~(libtorrent::alert::dht_notification
			+ libtorrent::alert::progress_notification
			+ libtorrent::alert::debug_notification
			+ libtorrent::alert::stats_notification));


	if (load_file(".ses_state", in, ec) == 0){
		libtorrent::lazy_entry e;
		if (libtorrent::lazy_bdecode(&in[0], &in[0] + in.size(), e, ec) == 0)
			ses.load_state(e);
	}

	// load the torrents
	std::vector<libtorrent::add_torrent_params> magnet_links;
	std::vector<std::string> torrents;
	int listen_port = 6881;
	int allocation_mode = libtorrent::storage_mode_sparse;
	std::string save_path(".");
	std::string bind_to_interface = "";
	settings.active_seeds = 0;
	settings.active_limit = 50;
	settings.active_downloads = 50;
	//libtorrent::sha1_hash info_hash;
	// test torrents: https://webtorrent.io/free-torrents
	std::string in_hash = argv[1];
	std::cout << "in_hash: " << in_hash << std::endl;
	libtorrent::add_torrent_params p;

	// add tracker
	// select from random (https://ngosang.github.io/trackerslist/trackers_all.txt)
	//std::string hardcode_tracker = "http://tracker.opentrackr.org:1337/announce";
	//p.trackers.push_back(hardcode_tracker.c_str() + 41);
	// add non hex info path
	//p.info_hash = info_hash;
	// save torrent to dev null
	p.save_path = save_path;
	int ret = mkdir(path_append(save_path, ".resume").c_str(), 0777);
	if (ret < 0)
		fprintf(stderr, "failed to create resume file directory: (%d) %s\n"
			, errno, strerror(errno));
	p.storage_mode = (libtorrent::storage_mode_t)allocation_mode;
	p.url = in_hash;
	libtorrent::add_torrent_params tmp;
	ec.clear();
	libtorrent::parse_magnet_uri(in_hash, tmp, ec);
	std::string filename = path_append(save_path, path_append(".resume"
		, libtorrent::to_hex(tmp.info_hash.to_string()) + ".resume"));
	load_file(filename.c_str(), p.resume_data, ec);
	magnet_links.push_back(p);

	// start torrent session
	if (start_lsd)
		ses.start_lsd();

	if (start_upnp){
		ses.start_upnp();
		ses.start_natpmp();
	}

	ses.set_proxy(ps);
	ses.listen_on(std::make_pair(listen_port, listen_port)
		, ec, bind_to_interface.c_str());
	if (ec){
		fprintf(stderr, "failed to listen%s%s on ports %d-%d: %s\n"
			, bind_to_interface.empty() ? "" : " on ", bind_to_interface.c_str()
			, listen_port, listen_port+1, ec.message().c_str());
	}

	libtorrent::dht_settings dht;
	dht.privacy_lookups = true;
	ses.set_dht_settings(dht);

	if (start_dht){
		settings.use_dht_as_fallback = false;
		ses.add_dht_router(std::make_pair(
			std::string("router.bittorrent.com"), 6881));
		ses.add_dht_router(std::make_pair(
			std::string("router.utorrent.com"), 6881));
		ses.add_dht_router(std::make_pair(
			std::string("router.bitcomet.com"), 6881));
		ses.start_dht();
	}

	settings.user_agent = "opentracker metadata crawler";
	settings.choking_algorithm = libtorrent::session_settings::auto_expand_choker;
	settings.disk_cache_algorithm = libtorrent::session_settings::avoid_readback;
	settings.volatile_read_cache = false;
	ses.set_settings(settings);

	for (std::vector<libtorrent::add_torrent_params>::iterator i = magnet_links.begin()
		, end(magnet_links.end()); i != end; ++i){
		ses.async_add_torrent(*i);
	}

	// main loop
	std::vector<libtorrent::peer_info> peers;
	std::vector<libtorrent::partial_piece_info> queue;
	int tick = 0;

	while (loop_limit > 1 || loop_limit == 0){
		++tick;

		//std::cout << "start loop" << std::endl;
		ses.post_torrent_updates();
		if (active_torrent >= int(filtered_handles.size())) active_torrent = filtered_handles.size() - 1;
		if (active_torrent >= 0)
		{
			// ask for distributed copies for the selected torrent. Since this
			// is a somewhat expensive operation, don't do it by default for
			// all torrents
			libtorrent::torrent_status const& h = *filtered_handles[active_torrent];
			h.handle.status(
				libtorrent::torrent_handle::query_distributed_copies
				| libtorrent::torrent_handle::query_pieces
				| libtorrent::torrent_handle::query_verified_pieces);
		}

		std::vector<libtorrent::feed_handle> feeds;
		ses.get_feeds(feeds);
		counters[torrents_feeds] = feeds.size();
		std::sort(filtered_handles.begin(), filtered_handles.end(), &compare_torrent);

		if (loop_limit > 1) --loop_limit;

		// loop through the alert queue to see if anything has happened.
		std::deque<libtorrent::alert*> alerts;
		ses.pop_alerts(&alerts);
		std::string now = timestamp();
		for (std::deque<libtorrent::alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i)
		{
			//std::cout << "in alert queue check" << std::endl;
			bool need_resort = false;
			TORRENT_TRY
			{
				//std::string event_string;
				//print_alert(*i, event_string);
				if (!::handle_alert(ses, *i, files, non_files, counters
					, all_handles, filtered_handles, need_resort))
				{
					// if we didn't handle the alert, print it to the log
					std::string event_string;
					print_alert(*i, event_string);
					events.push_back(event_string);
					if (events.size() >= 20) events.pop_front();
				}
			} TORRENT_CATCH(std::exception& e) {}

			if (need_resort)
			{
				std::sort(filtered_handles.begin(), filtered_handles.end()
					, &compare_torrent);
			}

			delete *i;
		}
		alerts.clear();
		/*libtorrent::session_status sess_stat = ses.status();
		// get metadata
		libtorrent::torrent_status const* st = 0;
		if (!filtered_handles.empty()) st = &get_active_torrent(filtered_handles);
		if (st && st->handle.is_valid())
		{
			std::cout << "here in get metadata" << std::endl;
			//std::string file_comment = st->handle.torrent_file()->comment();
			std::cout << "\t\tname:" << st->name << std::endl;
			//std::cout << "\t file_comment: " << file_comment << std::endl;
			//break;
		}*/

		libtorrent::sleep(1000);
	}
}