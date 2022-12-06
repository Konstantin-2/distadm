#pragma once
#include <map>
#include <set>
#include <string>
#include <mutex>
#include "sha.h"
#include "config.h"
#include "ccstream.h"
#include "usernames.h"

enum class NodeStatus : char {
	/* Does not do anything, only try to initialize from someone
	 * (copy matrix, status, no commands) */
	uninitialized,

	/* Partially initialized via offline file. Still can't do anything.
	 * Wait approvement of new UUID */
	part_init,

	// Main mode
	work,

	// Collect commands but not execute them, wait invited nodes response
	inviter,

	// Does not create new local commands
	deleting,

	// Does not run, exit
	deleted
};

extern const std::map<std::string, NodeStatus> distadm_statuses;
extern const std::map<NodeStatus, std::string> distadm_statuses_str;
void new_group(Config& cfg);
bool join_group(Config& cfg, const std::string&);

/* Command identifier
 * Each command has uuid of it's author node and sequential number
 * Class is used for commands ordering */
struct MsgId {
	MsgId() = default;
	MsgId(const Json::Value&);
	MsgId(const UUID&, size_t);
	bool operator<(const MsgId&) const;
	UUID node_id;
	size_t msg_number;
};

// Command to spread between nodes and execute on each of them
struct Msg : MsgId {
	Msg(const MsgId&);
	Msg(const UUID&, size_t);
	Msg(const Json::Value&);
	Json::Value as_json() const;
	size_t total_size() const; // Total size required to store command on disk (approximately)
	bool valid() const;

	Json::Value value;
	std::map<UUID, size_t> depends; // Commands to be executed before this one
	mutable bool delete_flag = false;
};

struct Node {
	enum class Intersting : char {
		unknown,
		yes,
		no
	};

	Node() = default;
	Node(const Json::Value&);

	// Update parameter with values from this node. Used in save()
	void update(Json::Value&) const;

	/* Commands of other nodes that are known to this node.
	 * Each node numerates it's commands sequentially.
	 * First element in this array is last known command number of first
	 * node in 'Core::nodes' that is known to this node, plus 1.
	 * Second element is about second node in 'Core::nodes' and so on.
	 * For example, matrix_row[2] = 7 means that this node knows all commands
	 * from node nodes.begin()+2 (MsgId::node_id == nodes.begin()+2)
	 * with numbers 0..6 (MsgId::cmd_number == 6). */
	std::vector<size_t> matrix_row;

	// Node state, hash of it's matrix
	SHA256 hash;

	/* Network message counter. When this node initiates network message
	 * transmission (UDP or TCP-client) it send this counter and increments it.
	 * So if other node receives message with counter smaller than known,
	 * this means possible network message spoofing. */
	size_t netmsgcnt = 0;

	// Command sequential identifier from this node to execute on host node
	size_t command_to_exec = 0;

	// Protocol version used by this node
	short proto_ver = 0;

	bool initialized = true;
	Intersting interesting = Intersting::no;
};

struct GroupIdPacket {
	UUID group_id;
	CryptKey key;
};

struct TrailerUUIDs {
	Nonce nonce;
	std::vector<UUID> uuids;
};

/* Matrix stores information about command known to other nodes (their
 * sequential numbers, as host node knows). Each row describe node (what
 * commands it knows). Each column describe source (author) node. So if i-row
 * and j-column contains value x this means that this host knows that i-node
 * knows all commands from j-node till x exclusively */
struct Matrix : std::map<UUID, Node> {
	// Get row for specified node
	Node& operator[](const UUID&);

	// Create new matrix 1x1
	void create(const UUID&);

	// Read matrix from parameter source
	static Matrix read(ICCstream&);

	// Read matrix with some additional data. Used in node initialization
	static Matrix read_vld(ICCstream&);

	// Load from json source. Used in load() at program startup
	static Matrix load_nodes(const Json::Value&);

	// Write matrix to parameter
	void write(OCCstream&) const;

	// Write matrix with some additional data. Used in node initialization
	void write_vld(OCCstream&) const;

	/* Update rows from src, new values is max(old-val, src-val)
	 * Nodes not presented in src keep their values
	 * Unknown nodes in source are ignored */
	bool update(const Matrix& src);

	// Add new nodes to matrix, they are copy of 'from_node'
	void resize(const UUID& new_ids, const Node* from_node, short proto_ver);
	void resize(std::vector<UUID>& new_ids, const Node* from_node, short proto_ver);

	// Del node from matrix
	void del(const UUID& id);

	/* Get offset of record with specified node from begin. Used to read column
	 * by it's node identifier */
	ssize_t node_offset(const UUID& id) const;
private:
	void write(OCCstream&, size_t) const;
};

// Core base contains common read-only info and is public for all inherited classes
struct CoreBase {
	CoreBase(Config&);

	Config& cfg;

	// Symmetric crypt key to crypt network traffic. Stored in file group-id
	CryptKey crypt_key;

	// Program group identifier
	UUID group_id;

	// This node identifier
	UUID my_id;

	// Return true if node is uninitialized or partially initialized
	bool need_initialize() const;

	// Return filename that can be used by daemon and try connect by client
	std::string unix_socket_name() const;

	// Return filename that can be used by daemon and try connect by low priveleged client
	std::string unix_socket_name_lp() const;

	// Load group_id and crypt_key. Return false on error
	bool load();

protected:
	// This is node status
	NodeStatus status;
};

/* All functions here are thread unsafe except static
 * Const functions can be run simultaneously */
struct Core : virtual CoreBase {
	Core(Config& c);

	// Load state. In case of errors print them to stderr and exit program
	void load();

	// Save state. In case of errors print them to stderr and exit program
	void save(bool force = false);

	// Create new group and initialize this node. Return true if created
	bool create_group();

	// Write group-id to invite file
	void write_group_id(Ostream&, const std::string& passwd) const;

	// Create small invite file for on-line initialization
	void write_online_invite(const std::string& filename, const std::string& passwd) const;

	// Create huge invite file for off-line initialization
	bool write_offline_invite(const std::string& filename, const std::string& passwd);

	// Read invite file. Return true if it was initialized
	bool read_invite(const std::string& filename, const std::string& passwd);

	// Complete invitation from "off-line" initializer file
	void finalize_offline_invitation(const std::string& filename);

	// Discard 'inviter' status
	void finalize_offline_invitation();

	// Does nothing. Used by inherited daemon, interrupt it when program status changed
	virtual void notify() const;

	// Return maximum protocol version supported by all nodes
	short max_proto_ver() const;

	// Read file exchanged by nodes
	void read_packet(const std::string& filename);

	// Write file exchanged by nodes
	void write_packet(const std::string& filename) const;

	/* Return name of node uuid.
	 * If there is known human readable hostname, return it.
	 * Otherwise return uuid as text string. */
	std::string nodename(const UUID&) const;

	// first parameter is element from 'node_states', second - UUID
	std::string nodename(const Json::Value&, const std::string&) const;

	// Retun UUID by UUID string or hostname or UUID::none()
	UUID nodebyname(const std::string&) const;

	// Return all nodenames as vector (see nodename()). Use in autocompletition
	std::vector<std::string> get_nodenames() const;

	// Return all usernames. Used in autocompletition
	std::vector<std::string> get_usernames();

	// Return all filenames as vector
	std::vector<std::string> get_filenames() const;

	void interactive(std::vector<std::string>& cmd);

	// Pass single command to daemon to execute
	bool interactive_online_exec(std::vector<std::string>& cmd, int fd);

	/* Interactive loop. Ask user to enter command and pass it to daemon
	 * If there is command in first parameter, pass it to daemon and return
	 * Second parameter is connection to daemon */
	void interactive_online(std::vector<std::string>& cmd, int fd);

	// Execute single command
	bool interactive_offline_exec(std::vector<std::string>& cmd);

	/* Interactive loop. Ask user to enter command and execute it
	 * If there is command in first parameter, execute it and return */
	void interactive_offline(std::vector<std::string>& cmd);

	// Execute command from first parameter, output to second. Return false means command is 'exit'
	bool interactive_exec(std::vector<std::string>& cmd, std::ostream& os);
	bool interactive_exec(std::vector<std::string>&& cmd, std::ostream& os);

	// Change to /var/local/distadm/files
	void cwd() const;

	/* Interactive processing of commands
	 * Interactive means that command is executed locally on daemon,
	 * it's output shown to user
	 * These functions are implemented in interactive.cpp */
	void incm_help(std::ostream&, std::vector<std::string>&);
	void incm_status(std::ostream&, std::vector<std::string>&);
	void incm_local_id(std::ostream&, std::vector<std::string>&);
	void incm_delnode(std::ostream&, std::vector<std::string>&);
	void incm_listnodes(std::ostream&, std::vector<std::string>&);
	void incm_addfile(std::ostream&, std::vector<std::string>&);
	void incm_delfile(std::ostream&, std::vector<std::string>&);
	void incm_deldir(std::ostream&, std::vector<std::string>&);
	void incm_listonline(std::ostream&, std::vector<std::string>&);
	void incm_exec(std::ostream&, std::vector<std::string>&);
	void incm_showexec(std::ostream&, std::vector<std::string>&);
	void incm_showlog(std::ostream&, std::vector<std::string>&);
	void incm_delexec(std::ostream&, std::vector<std::string>&);
	void incm_dellog(std::ostream&, std::vector<std::string>&);
	void incm_antivirus(std::ostream&, std::vector<std::string>&);
	void incm_adduser(std::ostream&, std::vector<std::string>&);
	void incm_deluser(std::ostream&, std::vector<std::string>&);
	void incm_listusers(std::ostream&, std::vector<std::string>&);
	void incm_listfiles(std::ostream&, std::vector<std::string>&);
	void incm_write_off_invite(std::ostream&, std::vector<std::string>&);
	void incm_write_on_invite(std::ostream&, std::vector<std::string>&);
	void incm_join(std::ostream&, std::vector<std::string>&);
	void incm_fin_invite(std::ostream&, std::vector<std::string>&);
	void incm_cancel_invite(std::ostream&, std::vector<std::string>&);
	void incm_read_packet(std::ostream&, std::vector<std::string>&);
	void incm_write_packet(std::ostream&, std::vector<std::string>&);
	void incm_queue(std::ostream&, std::vector<std::string>&);
	void incm_nodesinfo(std::ostream&, std::vector<std::string>&);
	void incm_stored_commands(std::ostream&, std::vector<std::string>&);

	/* Commands means commands that are distribued between nodes and executed
	 * as soon as they are become known to node
	 * These functions are implemented in commands.cpp */
	void exec(const Msg&);
	void exec_addnode(const Msg&);
	void exec_sethostname(const Msg&);
	void exec_delnode(const Msg&);
	void exec_delnoderecord(const Msg&);
	void exec_online(const Msg&);
	void exec_addfile(const Msg&);
	void exec_exec(const Msg&);
	void exec_executed(const Msg&);
	void exec_delexec(const Msg&);
	void exec_dellog(const Msg&);
	void exec_antivirus(const Msg&);
	void exec_adduser(const Msg&);
	void exec_deluser(const Msg&);
	void exec_smart(const Msg&);

	/* These functions executed before delete command
	 * Command deleted from node when it is executed and known to all other nodes
	 * These functions are implemented in bdcmd.cpp */
	void bdm_delnode(const Msg&);
	void bdm_delfile(const Msg&);
	void bdm_deldir(const Msg&);

protected:
	// Read file data after 'addfile' command
	void after_read(ICCstream& f, const Msg&);

	// Write file data after 'addfile' command
	void after_write(OCCstream&, const Msg&) const;

	void add_cmd(Msg&& cmd);

	// Update and return true if msgcnt is newer than known of node with uuid specified
	bool check_msg_cnt(const UUID&, size_t msgcnt);

	SHA256 calc_my_hash() const;

	const Msg * find_command(const MsgId& id) const;

	// Remove commands that are no more need to keep (i.e. known to all nodes)
	void remove_old_commands();

	virtual void delnoderecord(const UUID&);
	void read_net_initializer(ICCstream&, OCCstream&, const UUID& remote_id);
	void write_net_initializer(ICCstream&, OCCstream&);

	void update_node_hash(const UUID&, const SHA256&);

	void update_matrix(const Matrix&);

	void pending_commands();

	// Create new command entered by user into json. Store new command in 'commands'
	void create_command(Json::Value&&, bool add_depends = true);

	// Create command to tell hostname, online and antivirus
	void update_info();

	// Write antivirus info to low priveleged socket
	void write_av(int fd);

	bool node_known(const UUID&) const;

	// Return true if node is not found in log in dean nodes
	bool node_alive(const UUID&) const;

	// Delete self and stop program
	void del_self();

	Matrix nodes;

	Json::Value state_nodes;

	// points to row in 'nodes' variable
	Node* my_node = nullptr;

private:
	UUID read_initializer_v1(ICCstream&);
	void write_initializer_v1(OCCstream&);

	// Reads from data exchanged by nodes
	static std::set<Msg> read_messages(ICCstream&);
	TrailerUUIDs read_trailer_uuids(Istream&, size_t) const;
	GroupIdPacket read_group_id(Istream&, const std::string& passwd);
	void read_online_invite(const std::string& filename, const std::string& passwd);
	void read_offline_invite(const std::string& filename, const std::string& passwd);
	void read_packet_v1(ICCstream& f3);

	// Writes data to exchange by nodes
	void write_packet_v1(const std::string& filename) const;
	void write_messages(OCCstream&) const;
	void write_trailer_uuids(Ostream&, const TrailerUUIDs&) const;

	// Load group id from file. Return true if new group was created
	bool load_group_id();

	static NodeStatus load_status(const Json::Value&);
	static std::set<Msg> load_messages(const Json::Value&);
	static std::set<std::string> load_filenames(const Json::Value&);
	Json::Value load_local_state();
	Json::Value load_local_state(const std::string&);

	// Save group id to file (without any encryption)
	void save_group_id(const std::string& filename) const;

	// Return pointer to 'state_nodes' with updated info about nodes
	Json::Value& save_nodes();

	// Return json array with commands
	Json::Value save_commands() const;

	// Return json array with filenames
	Json::Value save_filenames() const;

	// Return text representation of this node status
	const std::string& status_string() const;

	// Return next command to exec or nullptr
	const Msg * command_to_exec() const;

	// Mark command as executed
	void mark_as_executed(const Msg&);

	// Return pointers to command which is unknown to specified nodes
	std::vector<const Msg*> commands_to_write(const std::set<UUID>& dest_nodes) const;

	// Executed before delete command. This command is to delete when it is known to all other nodes
	void before_delete_message(const Msg&);

	int create_unix_socket_client_fd() const;

	/* Execute new commandsand mark them a executed
	 * Return true if some commands was executed,
	 * false if nothisg was executed */
	bool execute_pending_commands();

	// Create command to tell everyone hostname of this node if it is changed
	void update_hostname();

	// Create command to tell everyone online date of this node if it is changed
	void update_online();

	// Create command to tell everyone antivirus status of this node if it is changed
	void update_antivirus();

	// Create command to tell everyone S.M.A.R.T. status of this node if it is changed
	void update_smart();

	void check_matrix();


	Json::Value state;
	std::set<std::string> filenames;
	Usernames users;
	std::set<Msg> messages;

	// id for off-line initialization file
	UUID invite_id;

	bool load_from_bkup = false;

	bool valid_node = false;

	bool need_save = false;

	// Count of iterations to hash password to create key to crypt invite file
	const unsigned pbkdf2_iter_count = 200;
};

extern Core * core;
