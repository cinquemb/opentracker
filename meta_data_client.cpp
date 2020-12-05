#include <iterator>
#include <stdio.h> // for snprintf
#include <stdlib.h> // for atoi

#include "<libtorrent/config.hpp>"

#include "<libtorrent/extensions/metadata_transfer.hpp>"
#include "<libtorrent/extensions/ut_metadata.hpp>"
#include "<libtorrent/extensions/ut_pex.hpp>"
#include "<libtorrent/extensions/smart_ban.hpp>"

#include "<libtorrent/entry.hpp>"
#include "<libtorrent/bencode.hpp>"
#include "<libtorrent/session.hpp>"
#include "<libtorrent/identify_client.hpp>"
#include "<libtorrent/alert_types.hpp>"
#include "<libtorrent/ip_filter.hpp>"
#include "<libtorrent/magnet_uri.hpp>"
#include "<libtorrent/bitfield.hpp>"
#include "<libtorrent/peer_info.hpp>"
#include "<libtorrent/time.hpp>"
#include "<libtorrent/create_torrent.hpp>"

libtorrent::torrent_status const& get_active_torrent(std::vector<libtorrent::torrent_status const*> const& filtered_handles)
{
	if (active_torrent >= int(filtered_handles.size())
		|| active_torrent < 0) active_torrent = 0;
	return *filtered_handles[active_torrent];
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

bool from_hex(char const *in, int len, char* out)
{
	for (char const* end = in + len; in < end; ++in, ++out)
	{
		int t = hex_to_int(*in);
		if (t == -1) return false;
		*out = t << 4;
		++in;
		t = hex_to_int(*in);
		if (t == -1) return false;
		*out |= t & 15;
	}
	return true;
}

enum {
	libtorrent::torrents_all,
	libtorrent::torrents_downloading,
	libtorrent::torrents_not_paused,
	libtorrent::torrents_seeding,
	libtorrent::torrents_queued,
	libtorrent::torrents_stopped,
	libtorrent::torrents_checking,
	libtorrent::torrents_feeds,

	libtorrent::torrents_max
};

int main(int argc, char* argv[])
{

	libtorrent::session_settings settings;

	libtorrent::proxy_settings ps;

	int refresh_delay = 1000;
	bool start_dht = true;
	bool start_upnp = true;
	bool start_lsd = true;
	int loop_limit = 0;

	std::deque<std::string> events;

	libtorrent::ptime next_dir_scan = time_now();

	// the string is the filename of the .torrent file, but only if
	// it was added through the directory monitor. It is used to
	// be able to remove torrents that were added via the directory
	// monitor when they're not in the directory anymore.
	std::vector<torrent_status const*> filtered_handles;

	handles_t files;
	// torrents that were not added via the monitor dir
	std::set<torrent_handle> non_files;

	int counters[libtorrent::torrents_max];
	memset(counters, 0, sizeof(counters));

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
	int torrent_upload_limit = 0;
	int torrent_download_limit = 0;
	std::string monitor_dir;
	std::string bind_to_interface = "";
	std::string outgoing_interface = "";
	int poll_interval = 5;
	int max_connections_per_torrent = 50;
	bool seed_mode = false;

	bool share_mode = false;
	bool disable_storage = false;

	// if non-empty, a peer that will be added to all torrents
	std::string peer;

	settings.active_seeds = 0;
	settings.active_limit = 0;

	//TODO 
	libtorrent::sha1_hash info_hash;
	from_hex(argv[i], 40, (char*)&info_hash[0]);

	libtorrent::add_torrent_params p;
	// distable storage
	p.storage = libtorrent::disabled_storage_constructor;
	// disable share mode
	p.flags |= libtorrent::add_torrent_params::flag_share_mode;
	// add tracker
	p.trackers.push_back(argv[i] + 41);
	// add non hex info path
	p.info_hash = info_hash;
	// save torrent to dev null
	p.save_path = "/dev/null";
	p.storage_mode = (libtorrent::storage_mode_t)allocation_mode;
	p.flags |= libtorrent::add_torrent_params::flag_paused;
	p.flags &= ~libtorrent::add_torrent_params::flag_duplicate_is_error;
	p.flags |= libtorrent::add_torrent_params::flag_auto_managed;
	magnet_links.push_back(p);
	libtorrent::error_code ec;

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
	if (ec)
	{
		fprintf(stderr, "failed to listen%s%s on ports %d-%d: %s\n"
			, bind_to_interface.empty() ? "" : " on ", bind_to_interface.c_str()
			, listen_port, listen_port+1, ec.message().c_str());
	}


	libtorrent::dht_settings dht;
	dht.privacy_lookups = true;
	ses.set_dht_settings(dht);

	if (start_dht)
	{
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
		, end(magnet_links.end()); i != end; ++i)
	{
		ses.async_add_torrent(*i);
	}

	// main loop
	std::vector<peer_info> peers;
	std::vector<partial_piece_info> queue;
	int tick = 0;


	while (loop_limit > 1 || loop_limit == 0)
	{
		++tick;
		ses.post_torrent_updates();
		if (active_torrent >= int(filtered_handles.size())) active_torrent = filtered_handles.size() - 1;
		if (active_torrent >= 0)
		{
			// ask for distributed copies for the selected torrent. Since this
			// is a somewhat expensive operation, don't do it by default for
			// all torrents
			libtorrent::torrent_status const& h = *filtered_handles[active_torrent];
			h.handle.status(
				torrent_handle::query_distributed_copies
				| torrent_handle::query_pieces
				| torrent_handle::query_verified_pieces);
		}

		std::vector<feed_handle> feeds;
		ses.get_feeds(feeds);

		counters[torrents_feeds] = feeds.size();

		std::sort(filtered_handles.begin(), filtered_handles.end(), &compare_torrent);

		if (loop_limit > 1) --loop_limit;
		session_status sess_stat = ses.status();
		// get metadata
		torrent_status const* st = 0;
		if (!filtered_handles.empty()) st = &get_active_torrent(filtered_handles);
		if (st && st->handle.is_valid())
		{
			std::string file_comment = st->handle.torrent_file()->comment();

			printf("metadata\n", st.name.c_str());
			std::cout << "\t file_comment: " << file_comment << std::endl;
			break;
		}
	}
}