/* 工作区与 Git/技术栈探测。 */

#include "workspace_probe.h"

#include "paths.h"
#include "linux/kernel.h"

#include <fcntl.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static bool file_exists(const char *path)
{
	return path && access(path, F_OK) == 0;
}

static bool directory_exists(const char *path)
{
	return path && access(path, X_OK) == 0;
}

static bool directory_has_entries(const char *path)
{
	DIR *dir;
	struct dirent *entry;

	if (!directory_exists(path)) {
		return false;
	}

	dir = opendir(path);
	if (!dir) {
		return false;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") != 0 &&
		    strcmp(entry->d_name, "..") != 0) {
			closedir(dir);
			return true;
		}
	}

	closedir(dir);
	return false;
}

static bool file_exists_under(const char *root, const char *name)
{
	char path[1200];

	if (!root || !root[0] || !name || !name[0]) {
		return false;
	}
	if (snprintf(path, sizeof(path), "%s/%s", root, name) >= (int)sizeof(path)) {
		return false;
	}
	return file_exists(path);
}

static bool read_process_output(char *out, size_t out_size, const char *program,
				char *const argv[])
{
	int pipefd[2];

	if (!out || out_size == 0 || !program || !argv) {
		return false;
	}
	out[0] = '\0';
	if (pipe(pipefd) != 0) {
		return false;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	}

	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);
		execvp(program, argv);
		_exit(127);
	}

	close(pipefd[1]);
	ssize_t n = read(pipefd[0], out, out_size - 1);
	close(pipefd[0]);
	int status = 0;
	waitpid(pid, &status, 0);

	if (n <= 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		out[0] = '\0';
		return false;
	}
	out[n] = '\0';

	size_t len = strlen(out);
	while (len > 0 &&
	       (out[len - 1] == '\n' || out[len - 1] == '\r' || out[len - 1] == ' ')) {
		out[--len] = '\0';
	}
	return out[0] != '\0';
}

static bool read_git_first_line(char *out, size_t out_size, char *const argv[])
{
	char *newline;

	if (!read_process_output(out, out_size, "git", argv)) {
		return false;
	}
	newline = strchr(out, '\n');
	if (newline) {
		*newline = '\0';
	}
	return out[0] != '\0';
}

static int run_process_quiet(const char *program, char *const argv[])
{
	if (!program || !argv) {
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		return -1;
	}

	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		execvp(program, argv);
		_exit(127);
	}

	int status = 0;
	if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status)) {
		return -1;
	}
	return WEXITSTATUS(status);
}

static bool remove_repo_path_recursive(const char *repo_path)
{
	char *rm_argv[] = {"rm", "-rf", (char *)repo_path, NULL};
	char workspace_prefix[1200];
	char probe_prefix[1200];
	size_t workspace_prefix_len;
	size_t probe_prefix_len;

	if (!repo_path || !repo_path[0]) {
		return false;
	}
	if (snprintf(workspace_prefix, sizeof(workspace_prefix), "%s/",
		     path_workspace_dir()) >= (int)sizeof(workspace_prefix)) {
		return false;
	}
	if (snprintf(probe_prefix, sizeof(probe_prefix), "%s/self_test_repo_probes/",
		     path_memory_dir()) >= (int)sizeof(probe_prefix)) {
		return false;
	}
	workspace_prefix_len = strlen(workspace_prefix);
	probe_prefix_len = strlen(probe_prefix);
	if (strncmp(repo_path, workspace_prefix, workspace_prefix_len) != 0 &&
	    strncmp(repo_path, probe_prefix, probe_prefix_len) != 0) {
		return false;
	}
	return run_process_quiet("rm", rm_argv) == 0;
}

static void cleanup_legacy_opencode_full_clone(void)
{
	char legacy_path[1200];

	if (snprintf(legacy_path, sizeof(legacy_path), "%s/opencode_full",
		     path_workspace_dir()) >= (int)sizeof(legacy_path)) {
		return;
	}
	if (!file_exists(legacy_path)) {
		return;
	}
	if (!remove_repo_path_recursive(legacy_path)) {
		pr_warn("workspace_probe: failed to remove legacy self-test repo path=%s",
			legacy_path);
		return;
	}
	pr_info("workspace_probe: removed legacy self-test repo path=%s", legacy_path);
}

static bool build_repo_path(const char *repo_name, char *repo_path, size_t repo_path_size)
{
	const char *root_dir = NULL;

	if (!repo_name || !repo_name[0] || !repo_path || repo_path_size == 0) {
		return false;
	}
	if (strcmp(repo_name, "opencode_probe") == 0) {
		static char probe_root[1200];

		if (snprintf(probe_root, sizeof(probe_root), "%s/self_test_repo_probes",
			     path_memory_dir()) >= (int)sizeof(probe_root)) {
			return false;
		}
		root_dir = probe_root;
	} else {
		root_dir = path_workspace_dir();
	}
	if (snprintf(repo_path, repo_path_size, "%s/%s",
		     root_dir, repo_name) >= (int)repo_path_size) {
		return false;
	}
	return true;
}

static bool repo_sources_look_present(const char *repo_path)
{
	static const char *markers[] = {
		".git",
		"package.json",
		"Cargo.toml",
		"go.mod",
		"pyproject.toml",
		"CMakeLists.txt",
		"README.md",
		"packages",
		"src",
	};
	char path[1200];

	if (!directory_exists(repo_path)) {
		return false;
	}

	for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
		if (snprintf(path, sizeof(path), "%s/%s", repo_path, markers[i]) >=
		    (int)sizeof(path)) {
			continue;
		}
		if (file_exists(path) || directory_exists(path)) {
			return true;
		}
	}

	return false;
}

static bool repo_path_has_dir(const char *repo_path, const char *relative_path)
{
	char path[1200];

	if (!repo_path || !repo_path[0] || !relative_path || !relative_path[0]) {
		return false;
	}
	if (snprintf(path, sizeof(path), "%s/%s", repo_path, relative_path) >=
	    (int)sizeof(path)) {
		return false;
	}
	return directory_exists(path);
}

static bool repo_path_has_file(const char *repo_path, const char *relative_path)
{
	char path[1200];

	if (!repo_path || !repo_path[0] || !relative_path || !relative_path[0]) {
		return false;
	}
	if (snprintf(path, sizeof(path), "%s/%s", repo_path, relative_path) >=
	    (int)sizeof(path)) {
		return false;
	}
	return file_exists(path);
}

static bool repo_matches_expected_layout(const char *repo_name, const char *repo_path)
{
	if (!repo_name || !repo_name[0]) {
		return repo_sources_look_present(repo_path);
	}

	if (strcmp(repo_name, "opencode") == 0 ||
	    strcmp(repo_name, "opencode_probe") == 0) {
		return repo_path_has_dir(repo_path, ".git") &&
		       repo_path_has_file(repo_path, "package.json") &&
		       repo_path_has_dir(repo_path, "packages") &&
		       repo_path_has_dir(repo_path, "packages/app") &&
		       repo_path_has_dir(repo_path, "packages/cli") &&
		       repo_path_has_dir(repo_path, "packages/session-ui");
	}

	return repo_sources_look_present(repo_path);
}

static void append_stack_item(char *stack, size_t stack_size, const char *item)
{
	if (!stack || stack_size == 0 || !item || !item[0]) {
		return;
	}
	if (stack[0]) {
		strncat(stack, ", ", stack_size - strlen(stack) - 1);
	}
	strncat(stack, item, stack_size - strlen(stack) - 1);
}

bool workspace_probe_collect(workspace_probe_result_t *result)
{
	char *branch_args[] = {"git", "branch", "--show-current", NULL};
	char *root_args[] = {"git", "rev-parse", "--show-toplevel", NULL};
	char *status_args[] = {"git", "diff-index", "--quiet", "HEAD", "--", NULL};
	char *commit_args[] = {"git", "log", "--oneline", "-1", NULL};
	const char *project_root;
	int status_exit;

	if (!result) {
		return false;
	}
	memset(result, 0, sizeof(*result));

	if (getcwd(result->cwd, sizeof(result->cwd))) {
		result->has_cwd = true;
	}
	strscpy(result->agent_workspace, path_workspace_dir(), sizeof(result->agent_workspace));

	result->has_branch = read_git_first_line(result->branch, sizeof(result->branch), branch_args);
	result->has_repo_root = read_git_first_line(result->repo_root, sizeof(result->repo_root),
						 root_args);
	status_exit = run_process_quiet("git", status_args);
	result->has_status = status_exit == 0 || status_exit == 1;
	result->is_dirty = status_exit == 1;
	result->has_commit = read_git_first_line(result->commit, sizeof(result->commit), commit_args);
	result->has_git = result->has_branch || result->has_repo_root || result->has_status ||
			 result->has_commit;

	project_root = result->has_repo_root ? result->repo_root :
		       (result->has_cwd ? result->cwd : NULL);
	if (project_root) {
		if (file_exists_under(project_root, "CMakeLists.txt")) {
			append_stack_item(result->stack, sizeof(result->stack), "C/CMake");
		}
		if (file_exists_under(project_root, "package.json")) {
			append_stack_item(result->stack, sizeof(result->stack), "Node.js");
		}
		if (file_exists_under(project_root, "tsconfig.json")) {
			append_stack_item(result->stack, sizeof(result->stack), "TypeScript");
		}
		if (file_exists_under(project_root, "go.mod")) {
			append_stack_item(result->stack, sizeof(result->stack), "Go");
		}
		if (file_exists_under(project_root, "Cargo.toml")) {
			append_stack_item(result->stack, sizeof(result->stack), "Rust");
		}
		if (file_exists_under(project_root, "pyproject.toml") ||
		    file_exists_under(project_root, "requirements.txt")) {
			append_stack_item(result->stack, sizeof(result->stack), "Python");
		}
		if (file_exists_under(project_root, "docker-compose.yml") ||
		    file_exists_under(project_root, "Dockerfile")) {
			append_stack_item(result->stack, sizeof(result->stack), "Docker");
		}
	}

	return result->has_cwd;
}

bool workspace_probe_repo_ready(const char *repo_name, char *repo_path, size_t repo_path_size)
{
	if (!repo_name || !repo_name[0]) {
		return false;
	}
	if (!build_repo_path(repo_name, repo_path, repo_path_size)) {
		return false;
	}
	return repo_matches_expected_layout(repo_name, repo_path);
}

bool workspace_probe_ensure_repo_clone(const char *repo_name, const char *clone_url,
				       char *repo_path, size_t repo_path_size)
{
	char *clone_argv[] = {"git", "clone", "--depth=1", (char *)clone_url, NULL, NULL};
	char repo_parent[1200];
	char *mkdir_argv[] = {"mkdir", "-p", repo_parent, NULL};
	int attempts = 0;
	char *slash = NULL;

	if (!repo_name || !repo_name[0] || !clone_url || !clone_url[0]) {
		return false;
	}
	if (!build_repo_path(repo_name, repo_path, repo_path_size)) {
		return false;
	}
	if (workspace_probe_repo_ready(repo_name, repo_path, repo_path_size)) {
		pr_info("workspace_probe: repo already ready repo=%s path=%s",
			repo_name, repo_path);
		return true;
	}
	if (file_exists(repo_path) && !directory_exists(repo_path)) {
		pr_warn("workspace_probe: repo path is file repo=%s path=%s",
			repo_name, repo_path);
		return false;
	}
	strscpy(repo_parent, repo_path, sizeof(repo_parent));
	slash = strrchr(repo_parent, '/');
	if (!slash || slash == repo_parent) {
		pr_warn("workspace_probe: invalid repo parent repo=%s path=%s",
			repo_name, repo_path);
		return false;
	}
	*slash = '\0';
	if (run_process_quiet("mkdir", mkdir_argv) != 0) {
		pr_warn("workspace_probe: failed to ensure repo parent dir for repo=%s path=%s",
			repo_name, repo_parent);
		return false;
	}
	if (directory_exists(repo_path) && directory_has_entries(repo_path)) {
		if (repo_matches_expected_layout(repo_name, repo_path)) {
			pr_info("workspace_probe: existing repo matches layout repo=%s path=%s",
				repo_name, repo_path);
			return true;
		}
		pr_warn("workspace_probe: removing mismatched repo dir repo=%s path=%s",
			repo_name, repo_path);
		if (!remove_repo_path_recursive(repo_path)) {
			pr_warn("workspace_probe: failed to remove mismatched repo dir repo=%s path=%s",
				repo_name, repo_path);
			return false;
		}
	}
	if (file_exists(repo_path) && !directory_exists(repo_path)) {
		pr_warn("workspace_probe: repo path became non-directory repo=%s path=%s",
			repo_name, repo_path);
		return false;
	}
	clone_argv[4] = repo_path;

	for (attempts = 0; attempts < 2; attempts++) {
		if (run_process_quiet("git", clone_argv) == 0 &&
		    repo_matches_expected_layout(repo_name, repo_path)) {
			pr_info("workspace_probe: remote clone ok repo=%s path=%s attempt=%d",
				repo_name, repo_path, attempts + 1);
			return true;
		}
		pr_warn("workspace_probe: remote clone failed or layout mismatch repo=%s path=%s attempt=%d",
			repo_name, repo_path, attempts + 1);
		if (directory_exists(repo_path) &&
		    !repo_matches_expected_layout(repo_name, repo_path)) {
			if (!remove_repo_path_recursive(repo_path)) {
				pr_warn("workspace_probe: failed to clean repo after remote clone mismatch repo=%s path=%s",
					repo_name, repo_path);
				return false;
			}
		}
	}

	return false;
}

bool workspace_probe_opencode_repo_ready(char *repo_path, size_t repo_path_size)
{
	return workspace_probe_repo_ready("opencode", repo_path, repo_path_size);
}

bool workspace_probe_ensure_opencode_repo(char *repo_path, size_t repo_path_size)
{
	return workspace_probe_ensure_repo_clone("opencode",
						 "https://github.com/sst/opencode.git",
						 repo_path,
						 repo_path_size);
}

bool workspace_probe_prepare_opencode_repo(workspace_probe_repo_prepare_t *result)
{
	if (!result) {
		return false;
	}

	cleanup_legacy_opencode_full_clone();
	memset(result, 0, sizeof(*result));
	result->repo_present_before = workspace_probe_opencode_repo_ready(
		result->repo_path, sizeof(result->repo_path));
	result->repo_ready_after = result->repo_present_before;
	if (!result->repo_ready_after) {
		result->repo_ready_after = workspace_probe_ensure_opencode_repo(
			result->repo_path, sizeof(result->repo_path));
	}
	return result->repo_ready_after;
}
