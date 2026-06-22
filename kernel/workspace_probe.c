/* 工作区与 Git/技术栈探测。 */

#include "workspace_probe.h"

#include "paths.h"
#include "linux/kernel.h"

#include <fcntl.h>
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
