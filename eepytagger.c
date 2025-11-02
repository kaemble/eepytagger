#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>

#define MAX_LINE 1024
#define MAX_ENTRIES 1000
#define MAX_FILENAME 256
#define DEFAULT_TEMP_FILE "/tmp/timestamps.txt"
#define DEFAULT_OUTPUT_FILE "timestamps.txt"

typedef struct {
	int seconds;
	char text[MAX_LINE];
} TagEntry;

// Format seconds into HH:MM:SS string
void format_time(int seconds, char *buffer, size_t buffer_size) {
	if (seconds < 0) seconds = 0;
	int h = seconds / 3600;
	int m = (seconds % 3600) / 60;
	int s = seconds % 60;
	snprintf(buffer, buffer_size, "%02d:%02d:%02d", h, m, s);
}

// Save entries to file
int save_to_file(const char *filename, TagEntry *entries, int count, int include_index) {
	FILE *out = fopen(filename, "w");
	if (!out) {
		perror("Failed to open file for saving");
		return -1;
	}
	for (int i = 0; i < count; i++) {
		char timestamp[16];
		format_time(entries[i].seconds, timestamp, sizeof(timestamp));
		if (include_index) {
			fprintf(out, "%2d. %s %s\n", i + 1, timestamp, entries[i].text);
		} else {
			fprintf(out, "%s %s\n", timestamp, entries[i].text);
		}
	}
	fclose(out);
	return 0;
}

// Load entries from file
int load_from_file(const char *filename, TagEntry *entries) {
	FILE *in = fopen(filename, "r");
	if (!in) {
		perror("Failed to open file for loading");
		return 0;
	}

	int count = 0;
	char line[MAX_LINE];
	while (fgets(line, sizeof(line), in) && count < MAX_ENTRIES) {
		char ts[16], rest[MAX_LINE];
		int dummy_index = 0;

		// Try parsing with or without index
		if (sscanf(line, "%d. %15s %[^\n]", &dummy_index, ts, rest) == 3 ||
			sscanf(line, "%15s %[^\n]", ts, rest) == 2) {
			int h, m, s;
			if (sscanf(ts, "%d:%d:%d", &h, &m, &s) == 3) {
				if (h < 0 || m < 0 || m > 59 || s < 0 || s > 59) {
					fprintf(stderr, "Invalid timestamp in file: %s\n", ts);
					continue;
				}
				long seconds = (long)h * 3600 + m * 60 + s;
				if (seconds > INT_MAX) {
					fprintf(stderr, "Timestamp too large: %s\n", ts);
					continue;
				}
				entries[count].seconds = (int)seconds;
				strncpy(entries[count].text, rest, MAX_LINE - 1);
				entries[count].text[MAX_LINE - 1] = '\0';
				count++;
			}
		}
	}
	fclose(in);
	return count;
}

void print_help() {
	printf("\n--- eepytagger v1.04 ---\n");
	printf("Commands:\n");
	printf("  !start [HH:MM:SS]                Start a tagging session, optionally setting an initial timestamp offset.\n");
	printf("  !end                             End the tagging session and save to the output file.\n");
	printf("  !offset <n>/all +/-<seconds>     Adjust the timestamp of tag(s) <n>/all by +/- seconds.\n");
	printf("  !previous +/-<seconds>           Adjust the timestamp of the last tag by +/- seconds.\n");
	printf("  !p +/-<seconds>                  Same as !previous.\n");
	printf("  !e <n> <new text>                Change the text of tag <n>, if <n> is not provided it edits the last tag,\n");
	printf("                                   '$' represents the previous version of the tag (can be escaped).\n");
	printf("  !pause                           Pauses the timer.\n");
	printf("  !resume                          Resumes the timer.\n");
	printf("  !delete <n>                      Delete tag <n>.\n");
	printf("  !help                            Show this help message.\n");
	printf("  <any text>                       Add a new tag with the current timestamp and the input text.\n");
	printf("\nCommand-line arguments:\n");
	printf("  -f <output_file>                 Specify output file (default: %s).\n", DEFAULT_OUTPUT_FILE);
	printf("  -t <temp_file>                   Specify temporary file (default: %s).\n", DEFAULT_TEMP_FILE);
	printf("  --resume <file>                  Resume tagging from an existing file.\n");
	printf("\nUse up/down arrow keys to cycle through command history.\n");
	printf("Maximum %d tags allowed.\n", MAX_ENTRIES);
	printf("------------------------\n\n");
}

char *trim_whitespace(char *str) {
	while (isspace((unsigned char)*str)) str++;
	char *end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) *end-- = '\0';
	return str;
}

int main(int argc, char *argv[]) {
	char output_filename[MAX_FILENAME] = DEFAULT_OUTPUT_FILE;
	char temp_filename[MAX_FILENAME] = DEFAULT_TEMP_FILE;
	TagEntry entries[MAX_ENTRIES] = {0}; // Initialize array
	int entry_count = 0;
	int resume_mode = 0;
	int started = 0;
	time_t start_time = 0;
	int paused = 0;
	time_t pause_time = 0;
	int paused_duration = 0;


	// Parse arguments
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
			if (strlen(argv[i + 1]) >= MAX_FILENAME) {
				fprintf(stderr, "Output filename too long\n");
				return 1;
			}
			strncpy(output_filename, argv[++i], MAX_FILENAME - 1);
			output_filename[MAX_FILENAME - 1] = '\0';
		} else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
			if (strlen(argv[i + 1]) >= MAX_FILENAME) {
				fprintf(stderr, "Temporary filename too long\n");
				return 1;
			}
			strncpy(temp_filename, argv[++i], MAX_FILENAME - 1);
			temp_filename[MAX_FILENAME - 1] = '\0';
		} else if (strcmp(argv[i], "--resume") == 0 && i + 1 < argc) {
			if (strlen(argv[i + 1]) >= MAX_FILENAME) {
				fprintf(stderr, "Resume filename too long\n");
				return 1;
			}
			strncpy(output_filename, argv[++i], MAX_FILENAME - 1);
			output_filename[MAX_FILENAME - 1] = '\0';
			entry_count = load_from_file(output_filename, entries);
			resume_mode = 1;
		} else {
			print_help();
			return 1;
		}
	}

	// Check if temporary file directory is writable
	char *tmp_dir = getenv("TMPDIR");
	if (!tmp_dir) tmp_dir = "/tmp";
	if (access(tmp_dir, W_OK) != 0) {
		fprintf(stderr, "Cannot write to temporary directory %s\n", tmp_dir);
		return 1;
	}

	// Initialize readline history
	using_history();

	printf("eepytagger ready. Use !start [HH:MM:SS] to begin.\n");
	printf("Output file: %s\n", output_filename);
	printf("Temporary file: %s\n", temp_filename);

	char *line;
	while (1) {
		line = readline("> ");
		if (!line) {
			fprintf(stderr, "Memory allocation failed or EOF received\n");
			break;
		}
		if (*line) add_history(line);

		// Copy and trim input
		char input[MAX_LINE];
		strncpy(input, line, MAX_LINE - 1);
		input[MAX_LINE - 1] = '\0';
		free(line);
		char *trimmed_input = trim_whitespace(input);
		if (*trimmed_input == '\0') {
			continue;
		}

		if (strcmp(trimmed_input, "!help") == 0) {
			print_help();
			continue;
		}

		if (strcmp(trimmed_input, "!end") == 0) break;

		if (strncmp(trimmed_input, "!start", 6) == 0) {
			int h = 0, m = 0, s = 0;
			int initial_seconds = 0;
			if (sscanf(trimmed_input, "!start %d:%d:%d", &h, &m, &s) == 3) {
				if (h < 0 || m < 0 || m > 59 || s < 0 || s > 59) {
					printf("Invalid timestamp format. Use HH:MM:SS with valid values.\n");
					continue;
				}
				long seconds = (long)h * 3600 + m * 60 + s;
				if (seconds > INT_MAX) {
					printf("Timestamp too large.\n");
					continue;
				}
				initial_seconds = (int)seconds; // Set initial elapsed time
			} else if (strcmp(trimmed_input, "!start") != 0) {
				printf("Invalid !start format. Use !start [HH:MM:SS].\n");
				continue;
			} // If just !start, initial_seconds remains 0
			start_time = time(NULL) - initial_seconds;
			started = 1;
			printf("Started tagging from %02d:%02d:%02d\n", initial_seconds / 3600, (initial_seconds % 3600) / 60, initial_seconds % 60);
			continue;
		}

		if (strcmp(trimmed_input, "!pause") == 0) {
			if (!started) {
				printf("Session not started yet.\n");
				continue;
			}
			if (paused) {
				printf("Already paused.\n");
				continue;
			}
			pause_time = time(NULL);
			paused = 1;
			int elapsed = (int)(pause_time - start_time - paused_duration);
			if (elapsed < 0) elapsed = 0;
			printf("Paused at %02d:%02d:%02d\n", elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60);
			continue;
		}

		if (strcmp(trimmed_input, "!resume") == 0) {
			if (!started) {
				printf("Session not started yet.\n");
				continue;
			}
			if (!paused) {
				printf("Not currently paused.\n");
				continue;
			}
			time_t now = time(NULL);
			paused_duration += (int)(now - pause_time);
			paused = 0;
			int elapsed = (int)(now - start_time - paused_duration);
			if (elapsed < 0) elapsed = 0;
			printf("Resumed at %02d:%02d:%02d\n", elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60);
			continue;
		}

		if (!started) {
			printf("Use !start [HH:MM:SS] before tagging.\n");
			continue;
		}

		// Handle offset commands
		int n, adj;
		char sign;
		char offset_type[16] = {0};

		if ((sscanf(trimmed_input, "!offset %d %c%d", &n, &sign, &adj) == 3 ||
			sscanf(trimmed_input, "!p %c%d", &sign, &adj) == 2 ||
			sscanf(trimmed_input, "!previous %c%d", &sign, &adj) == 2 ||
			sscanf(trimmed_input, "!offset %15s %c%d", offset_type, &sign, &adj) == 3)) {
			int delta = (sign == '-') ? -adj : adj;

			if (strncmp(trimmed_input, "!offset", 7) == 0 && strcmp(offset_type, "all") == 0) {
				// Adjust all tags
				if (entry_count == 0) {
					printf("No tags to adjust.\n");
					continue;
				}
				for (int i = 0; i < entry_count; i++) {
					int new_time = entries[i].seconds + delta;
					if (new_time < 0) {
						entries[i].seconds = 0;
						printf("Tag %d clamped to 00:00:00 (was negative after offset).\n", i + 1);
					} else {
						entries[i].seconds = new_time;
					}
				}
				printf("Adjusted all tags by %+d seconds.\n", delta);
			} else {
				// Adjust specific tag or last tag
				int index = (strncmp(trimmed_input, "!offset", 7) == 0 && offset_type[0] == '\0') ? n - 1 : entry_count - 1;
				if (index < 0 || index >= entry_count) {
					printf("Invalid tag index.\n");
					continue;
				}
				if (entries[index].seconds + delta < 0) {
					printf("Adjustment would result in negative timestamp.\n");
					continue;
				}
				entries[index].seconds += delta;
				char ts_buf[16];
				format_time(entries[index].seconds, ts_buf, sizeof(ts_buf));
				printf("Adjusted tag %d to %s\n", index + 1, ts_buf);
			}
			save_to_file(temp_filename, entries, entry_count, 1);
			continue;
		}

		// Handle !edit
		if (strncmp(trimmed_input, "!e", 2) == 0) {
			int edit_index = entry_count - 1;
			char *rest = trimmed_input + 2;
			rest = trim_whitespace(rest);
			if (isdigit((unsigned char)*rest)) {
				char *after_index = rest;
				edit_index = strtol(after_index, &after_index, 10) - 1;
				rest = trim_whitespace(after_index);
			}

			if (*rest == '\0') {
				printf("Usage: !e [n] new text\n");
				continue;
			}
			if (edit_index < 0 || edit_index >= entry_count) {
				printf("Invalid tag index.\n");
				continue;
			}
	
			// Replace $ with original tag text
			char new_text[MAX_LINE] = {0};
			const char *original = entries[edit_index].text;
			int new_pos = 0;
			for (int i = 0; rest[i] != '\0' && new_pos < MAX_LINE - 1; i++) {
				if (rest[i] == '\\' && rest[i + 1] == '$' && new_pos < MAX_LINE - 1) {
					new_text[new_pos++] = '$';
					i++;
				} else if (rest[i] == '$' && new_pos + strlen(original) < MAX_LINE - 1) {
					strcpy(new_text + new_pos, original);
					new_pos += strlen(original);
				} else {
					new_text[new_pos++] = rest[i];
				}
			}
			new_text[new_pos] = '\0';
			strncpy(entries[edit_index].text, new_text, MAX_LINE - 1);
			entries[edit_index].text[MAX_LINE - 1] = '\0';
			printf("Edited tag %d.\n", edit_index + 1);
			save_to_file(temp_filename, entries, entry_count, 1);
			continue;
		}

		if (strncmp(trimmed_input, "!delete ", 8) == 0) {
			int del_index;
			if (sscanf(trimmed_input + 8, "%d", &del_index) == 1) {
				del_index--;
				if (del_index < 0 || del_index >= entry_count) {
					printf("Invalid tag index.\n");
					continue;
				}
				for (int i = del_index; i < entry_count - 1; i++) {
					entries[i] = entries[i + 1];
				}
				entry_count--;
				printf("Deleted tag %d.\n", del_index + 1);
				save_to_file(temp_filename, entries, entry_count, 1);
				continue;
			} else {
				printf("Usage: !delete <n>\n");
				continue;
			}
		}

		// Check for invalid ! commands
		if (trimmed_input[0] == '!') {
			printf("Unknown command: %s. Use !help for a list of valid commands.\n", trimmed_input);
			continue;
		}

		// Add new tag
		time_t now = time(NULL);
		int effective_paused_duration = paused_duration;
		if (paused) {
			effective_paused_duration += (int)(now - pause_time);
			printf("Warning: tagging while paused.\n");
		}

		int elapsed = (int)(now - start_time - effective_paused_duration);
		if (elapsed < 0) elapsed = 0;

		if (entry_count >= MAX_ENTRIES) {
			printf("Maximum number of entries (%d) reached.\n", MAX_ENTRIES);
			break;
		}

		entries[entry_count].seconds = elapsed;
		strncpy(entries[entry_count].text, trimmed_input, MAX_LINE - 1);
		entries[entry_count].text[MAX_LINE - 1] = '\0';
		entry_count++;

		save_to_file(temp_filename, entries, entry_count, 1);
	}

	// Save final output and clean up
	if (entry_count > 0) {
		if (save_to_file(output_filename, entries, entry_count, 0) == 0) {
			printf("Saved final timestamps to %s\n", output_filename);
		}
	}
	return 0;
}
