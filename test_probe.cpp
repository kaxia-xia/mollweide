#include <cstdio>
#include <cstdlib>
#include <cstring>
int main() {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "ffprobe -v error -select_streams v:0 -show_entries stream=width,height,nb_frames,r_frame_rate "
        "-of default=noprint_wrappers=1 \"video/short_test.mp4\" 2>/dev/null");
    printf("Running: %s\n", cmd);
    FILE *fp = popen(cmd, "r");
    if (!fp) { printf("popen failed\n"); return 1; }
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        printf("LINE: %s", line);
    }
    int ret = pclose(fp);
    printf("pclose ret=%d\n", ret);
    return 0;
}
