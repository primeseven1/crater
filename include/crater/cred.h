#pragma once

struct cred {
	int uid, euid, suid;
	int gid, egid, sgid;
};

struct cred* current_cred(void);
