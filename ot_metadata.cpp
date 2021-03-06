/* Opentracker */
#include "ot_metadata.hpp"

FILE* g_log_file = 0;
int torrent_filter = torrents_not_paused;

std::string outgoing_interface = "";
int max_connections_per_torrent = 50;
int torrent_upload_limit = 0;
int torrent_download_limit = 0;
// if non-empty, a peer that will be added to all torrents
std::string peer;

/* torrent stuff below */

double get_time(){
    double cur_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() / 1000.;
    return cur_time;
}

std::string path_append(std::string const& lhs, std::string const& rhs)
{
	if (lhs.empty() || lhs == ".") return rhs;
	if (rhs.empty() || rhs == ".") return lhs;
#define TORRENT_SEPARATOR "/"
	bool need_sep = lhs[lhs.size()-1] != '/';
	return lhs + (need_sep?TORRENT_SEPARATOR:"") + rhs;
}

libtorrent::torrent_status const& get_active_torrent(std::vector<libtorrent::torrent_status const*> const& filtered_handles, int& active_torrent)
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
	, std::vector<libtorrent::torrent_status const*>& filtered_handles, int* counters, int& active_torrent)
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
}
// returns true if the alert was handled (and should not be printed to the log)
// returns false if the alert was not handled
bool handle_alert(libtorrent::session& ses, libtorrent::alert* a
	, handles_t& files, std::set<libtorrent::torrent_handle>& non_files
	, int* counters, boost::unordered_set<libtorrent::torrent_status>& all_handles
	, std::vector<libtorrent::torrent_status const*>& filtered_handles
	, bool& need_resort
	, bool& has_rec_metadata
	, int& num_outstanding_resume_data
	, int& active_torrent){
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
		h.resume();
	}
#endif

	if (libtorrent::metadata_received_alert* p = libtorrent::alert_cast<libtorrent::metadata_received_alert>(a))
	{
		// mark have recieved metadata
		libtorrent::torrent_handle h = p->handle;
		if (h.is_valid()) {
			has_rec_metadata = true;
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
			if (h.is_valid()
				&& non_files.find(h) == non_files.end()
				&& std::find_if(files.begin(), files.end()
					, boost::bind(&handles_t::value_type::second, _1) == h) == files.end())
				ses.remove_torrent(h);
		}
	}
	else if (libtorrent::save_resume_data_failed_alert* p = libtorrent::alert_cast<libtorrent::save_resume_data_failed_alert>(a))
	{
		--num_outstanding_resume_data;
		libtorrent::torrent_handle h = p->handle;
		if (h.is_valid()
			&& non_files.find(h) == non_files.end()
			&& std::find_if(files.begin(), files.end()
				, boost::bind(&handles_t::value_type::second, _1) == h) == files.end()){
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
		if (need_filter_update)
			update_filtered_torrents(all_handles, filtered_handles, counters, active_torrent);
		return true;
	}
	return false;
}

std::vector<std::string> get_info_hash_metadata(std::vector<std::string> magnet_uris){
	libtorrent::session_settings settings;
	libtorrent::proxy_settings ps;

	std::vector<std::string> output;

	int active_torrent = 0;
	double max_time_check = 30.0; //seconds
	
	// the number of times we've asked to save resume data
	// without having received a response (successful or failure)
	int num_outstanding_resume_data = 0;

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

	for (int i=0; i<magnet_uris.size(); i++){
		libtorrent::add_torrent_params p;
		// add tracker
		// select from random (https://ngosang.github.io/trackerslist/trackers_all.txt)
		p.save_path = save_path;
		p.storage_mode = (libtorrent::storage_mode_t)allocation_mode;
		p.url = magnet_uris[i];
		magnet_links.push_back(p);
	}

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
		ses.add_dht_router(std::make_pair(
			std::string("dht.transmissionbt.com"), 6881));
		ses.add_dht_router(std::make_pair(
			std::string("dht.aelitis.com"), 6881));
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
	int tick = 0;
	bool has_rec_metadata = false;

	double start_time = get_time();

	int total_count = magnet_uris.size();

	while (loop_limit > 1 || loop_limit == 0){
		++tick;

		//std::cout << "start loop" << std::endl;
		ses.post_torrent_updates();
		if (active_torrent >= int(filtered_handles.size())) active_torrent = filtered_handles.size() - 1;

		std::vector<libtorrent::feed_handle> feeds;
		ses.get_feeds(feeds);
		counters[torrents_feeds] = feeds.size();
		std::sort(filtered_handles.begin(), filtered_handles.end(), &compare_torrent);

		if (loop_limit > 1) --loop_limit;

		// loop through the alert queue to see if anything has happened.
		std::deque<libtorrent::alert*> alerts;
		ses.pop_alerts(&alerts);		
		for (std::deque<libtorrent::alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i){
			bool need_resort = false;
			TORRENT_TRY {
				handle_alert(ses, *i, files, non_files, counters, all_handles, filtered_handles, need_resort, has_rec_metadata, num_outstanding_resume_data, active_torrent);
			} TORRENT_CATCH(std::exception& e) {}

			if (need_resort){
				std::sort(filtered_handles.begin(), filtered_handles.end()
					, &compare_torrent);
			}
			delete *i;
		}
		alerts.clear();
		
		double cur_time = get_time();
		libtorrent::torrent_status const* st = 0;
		if (!filtered_handles.empty()) st = &get_active_torrent(filtered_handles, active_torrent);
		if(has_rec_metadata){
			if (st && st->handle.is_valid()){
				libtorrent::torrent_handle h = st->handle;
				boost::intrusive_ptr<libtorrent::torrent_info const> ti = h.torrent_file();
				std::string file_comment = ti->comment();
				std::string torrent_name = ti->name();
				std::string torrent_url = libtorrent::make_magnet_uri(h);
				std::string metadata = torrent_url + " -> " + torrent_name + " " + file_comment;
				std::cout << "active_torrent(s): " << active_torrent << " metadata: " << metadata << " dl_elapsed: "<<  (cur_time - start_time) << std::endl;
				ses.remove_torrent(h);
				output.push_back(metadata);
				has_rec_metadata = false;
				total_count--;
				update_filtered_torrents(all_handles, filtered_handles, counters, active_torrent);
			}
		} else {
			if (st && st->handle.is_valid()){
				if ((cur_time - start_time) > max_time_check){
					libtorrent::torrent_handle h = st->handle;
					std::string torrent_url = libtorrent::make_magnet_uri(h);
					ses.remove_torrent(h);
					std::cout << "\t\tskipping(s): " << torrent_url << " dl_wait: "<<  (cur_time - start_time) << std::endl;
					has_rec_metadata = false;
					start_time = get_time();
					total_count--;
					update_filtered_torrents(all_handles, filtered_handles, counters, active_torrent);
				}
			}
		}
		if (total_count == 0)
			return output;
		libtorrent::sleep(1000);
	}

	return output;
};
/* torrent stuff above */