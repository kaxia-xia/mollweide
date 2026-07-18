#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
int main() {
    // Test decoder pipe
    FILE *dec_pipe = popen("ffmpeg -y -i video/short_test.mp4 -f rawvideo -pix_fmt rgb24 - 2>/dev/null", "r");
    if (!dec_pipe) { printf("decoder popen failed\n"); return 1; }
    
    // Test encoder pipe
    FILE *enc_pipe = popen("ffmpeg -y -f rawvideo -pix_fmt rgb24 -s 1920x1080 -framerate 30 -i - -c:v libx264 -pix_fmt yuv420p -crf 23 -an output_test2.mp4 2>/dev/null", "w");
    if (!enc_pipe) { printf("encoder popen failed\n"); pclose(dec_pipe); return 1; }
    
    printf("Pipes opened successfully\n");
    
    // Read first frame
    std::vector<unsigned char> buf(1920 * 1080 * 3);
    size_t nread = fread(buf.data(), 1, buf.size(), dec_pipe);
    printf("Read %zu bytes (expected %zu)\n", nread, buf.size());
    
    if (nread == buf.size()) {
        // Write it out
        fwrite(buf.data(), 1, buf.size(), enc_pipe);
        printf("Wrote frame\n");
    }
    
    pclose(dec_pipe);
    int ret = pclose(enc_pipe);
    printf("Encoder pclose ret=%d\n", ret);
    return 0;
}
