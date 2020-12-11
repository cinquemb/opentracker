#ifndef OT_METADATA_H__
#define OT_METADATA_H__

#include <boost/unordered_set.hpp>

#include <libtorrent/config.hpp>
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

#include <iterator>

typedef std::multimap<std::string, libtorrent::torrent_handle> handles_t;

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

extern std::string path_append(std::string const& lhs, std::string const& rhs);
extern libtorrent::torrent_status const& get_active_torrent(std::vector<libtorrent::torrent_status const*> const& filtered_handles, int& active_torrent);
extern bool show_torrent(libtorrent::torrent_status const& st, int torrent_filter, int* counters);
extern bool compare_torrent(libtorrent::torrent_status const* lhs, libtorrent::torrent_status const* rhs);
extern void update_filtered_torrents(boost::unordered_set<libtorrent::torrent_status>& all_handles
	, std::vector<libtorrent::torrent_status const*>& filtered_handles, int* counters, int& active_torrent);
extern char const* timestamp();
extern char const* esc(char const* code);
extern void print_alert(libtorrent::alert const* a, std::string& str);
extern bool handle_alert(libtorrent::session& ses, libtorrent::alert* a, handles_t& files, std::set<libtorrent::torrent_handle>& non_files, int* counters, boost::unordered_set<libtorrent::torrent_status>& all_handles, std::vector<libtorrent::torrent_status const*>& filtered_handles, bool& need_resort, bool& has_rec_metadata, int& num_outstanding_resume_data, int& active_torrent);
extern std::vector<std::string> get_info_hash_metadata(std::vector<std::string>& magnet_uris);

#endif