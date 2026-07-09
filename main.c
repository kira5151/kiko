#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define MAX_RECORD_SECONDS 300
#define MAX_MEMOS 5
#define MEMO_LIST_FILE "memo_list.txt"
#define TITLE_MAX 128
/* アプリ内の再生音量。大きくするなら 20.0f や 30.0f に変更 */
#define PLAYBACK_GAIN 80.0f

typedef struct {
    SDL_Rect rect;
    const char *label;
} Button;

typedef struct {
    Uint8 *data;
    Uint32 length;
    Uint32 capacity;
    Uint32 maxBytes;
    SDL_AudioSpec spec;
} AudioBuffer;

AudioBuffer audioBuffer = {NULL, 0, 0, 0};

typedef enum {
    EFFECT_NORMAL = 0,
    EFFECT_HIGH,
    EFFECT_LOW,
    EFFECT_ECHO
} EffectMode;

const char *getEffectName(EffectMode effectMode) {
    if (effectMode == EFFECT_HIGH) {
        return "高音";
    }

    if (effectMode == EFFECT_LOW) {
        return "低音";
    }

    if (effectMode == EFFECT_ECHO) {
        return "エコー";
    }

    return "通常";
}

Sint16 clampSample(int value) {
    if (value > 32767) {
        return 32767;
    }

    if (value < -32768) {
        return -32768;
    }

    return (Sint16)value;
}


void writeLE16(FILE *fp, Uint16 value) {
    fputc(value & 0xff, fp);
    fputc((value >> 8) & 0xff, fp);
}

void writeLE32(FILE *fp, Uint32 value) {
    fputc(value & 0xff, fp);
    fputc((value >> 8) & 0xff, fp);
    fputc((value >> 16) & 0xff, fp);
    fputc((value >> 24) & 0xff, fp);
}

bool fileExists(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        return false;
    }
    fclose(fp);
    return true;
}

bool createNextMemoFilename(char *outFilename, int outSize) {
    for (int i = 1; i <= 999; i++) {
        snprintf(outFilename, outSize, "audio%d.wav", i);

        if (!fileExists(outFilename)) {
            return true;
        }
    }

    return false;
}

void removeLastUtf8Char(char *text) {
    int len = strlen(text);

    if (len <= 0) {
        return;
    }

    len--;

    while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80) {
        len--;
    }

    text[len] = '\0';
}

void appendTitleText(char *title, const char *addText) {
    if (addText == NULL) {
        return;
    }

    if (strlen(title) + strlen(addText) < TITLE_MAX - 1) {
        strcat(title, addText);
    }
}

bool saveWavFile(const char *filename, AudioBuffer *buffer) {
    if (buffer->data == NULL || buffer->length == 0) {
        return false;
    }

    FILE *fp = fopen(filename, "wb");
    if (fp == NULL) {
        printf("WAV保存エラー: ファイルを開けません\n");
        return false;
    }

    Uint16 audioFormat = 1;
    Uint16 channels = buffer->spec.channels;
    Uint32 sampleRate = buffer->spec.freq;
    Uint16 bitsPerSample = 16;
    Uint32 byteRate = sampleRate * channels * bitsPerSample / 8;
    Uint16 blockAlign = channels * bitsPerSample / 8;
    Uint32 dataSize = buffer->length;
    Uint32 chunkSize = 36 + dataSize;

    fwrite("RIFF", 1, 4, fp);
    writeLE32(fp, chunkSize);
    fwrite("WAVE", 1, 4, fp);

    fwrite("fmt ", 1, 4, fp);
    writeLE32(fp, 16);
    writeLE16(fp, audioFormat);
    writeLE16(fp, channels);
    writeLE32(fp, sampleRate);
    writeLE32(fp, byteRate);
    writeLE16(fp, blockAlign);
    writeLE16(fp, bitsPerSample);

    fwrite("data", 1, 4, fp);
    writeLE32(fp, dataSize);
    fwrite(buffer->data, 1, dataSize, fp);

    fclose(fp);
    return true;

}

bool createEffectAudioBuffer(AudioBuffer *source, AudioBuffer *result, EffectMode effectMode) {
    result->data = NULL;
    result->length = 0;
    result->capacity = 0;
    result->maxBytes = 0;
    result->spec = source->spec;

    if (source->data == NULL || source->length == 0) {
        return false;
    }

    int bytesPerSample = 2;
    int channels = source->spec.channels;
    int frameBytes = channels * bytesPerSample;
    int sourceFrames = source->length / frameBytes;

    if (source->spec.format != AUDIO_S16LSB || channels <= 0 || sourceFrames <= 0) {
        result->data = (Uint8 *)SDL_malloc(source->length);
        if (result->data == NULL) {
            return false;
        }

        SDL_memcpy(result->data, source->data, source->length);
        result->length = source->length;
        result->capacity = source->length;
        return true;
    }

    Uint32 resultLength = source->length;

    if (effectMode == EFFECT_HIGH) {
        resultLength = ((sourceFrames + 1) / 2) * frameBytes;
    } else if (effectMode == EFFECT_LOW) {
        resultLength = sourceFrames * 2 * frameBytes;
    }

    result->data = (Uint8 *)SDL_malloc(resultLength);
    if (result->data == NULL) {
        return false;
    }

    result->length = resultLength;
    result->capacity = resultLength;

    Sint16 *sourceSamples = (Sint16 *)source->data;
    Sint16 *resultSamples = (Sint16 *)result->data;

    if (effectMode == EFFECT_NORMAL) {
        SDL_memcpy(result->data, source->data, source->length);
    } else if (effectMode == EFFECT_HIGH) {
        int resultFrame = 0;

        for (int frame = 0; frame < sourceFrames; frame += 2) {
            for (int ch = 0; ch < channels; ch++) {
                resultSamples[resultFrame * channels + ch] = sourceSamples[frame * channels + ch];
            }
            resultFrame++;
        }
    } else if (effectMode == EFFECT_LOW) {
        int resultFrame = 0;

        for (int frame = 0; frame < sourceFrames; frame++) {
            for (int repeat = 0; repeat < 2; repeat++) {
                for (int ch = 0; ch < channels; ch++) {
                    resultSamples[resultFrame * channels + ch] = sourceSamples[frame * channels + ch];
                }
                resultFrame++;
            }
        }
    } else if (effectMode == EFFECT_ECHO) {
        SDL_memcpy(result->data, source->data, source->length);

        int sampleCount = sourceFrames * channels;
        int delayFrames = source->spec.freq / 4;
        int delaySamples = delayFrames * channels;

        for (int i = delaySamples; i < sampleCount; i++) {
            int mixed = resultSamples[i] + (int)(resultSamples[i - delaySamples] * 0.45);
            resultSamples[i] = clampSample(mixed);
        }
    }

    return true;
}

bool saveWavFileWithEffect(const char *filename, AudioBuffer *buffer, EffectMode effectMode) {
    AudioBuffer processed;

    if (!createEffectAudioBuffer(buffer, &processed, effectMode)) {
        return false;
    }

    bool result = saveWavFile(filename, &processed);

    if (processed.data != NULL) {
        SDL_free(processed.data);
    }

    return result;
}

bool saveMemoList(
    char memoFiles[MAX_MEMOS][64],
    int memoSeconds[MAX_MEMOS],
    char memoTitles[MAX_MEMOS][TITLE_MAX],
    int memoCount
) {
    FILE *fp = fopen(MEMO_LIST_FILE, "w");
    if (fp == NULL) {
        printf("メモ一覧保存エラー\n");
        return false;
    }

    for (int i = 0; i < memoCount; i++) {
        fprintf(fp, "%s\t%d\t%s\n", memoFiles[i], memoSeconds[i], memoTitles[i]);
    }

    fclose(fp);
    return true;
}

int loadMemoList(
    char memoFiles[MAX_MEMOS][64],
    int memoSeconds[MAX_MEMOS],
    char memoTitles[MAX_MEMOS][TITLE_MAX]
) {
    FILE *fp = fopen(MEMO_LIST_FILE, "r");
    if (fp == NULL) {
        return 0;
    }

    int count = 0;
    char line[512];

    while (count < MAX_MEMOS && fgets(line, sizeof(line), fp) != NULL) {
        char filename[64] = "";
        int seconds = 0;
        char title[TITLE_MAX] = "";

        line[strcspn(line, "\r\n")] = '\0';

        if (sscanf(line, "%63[^\t]\t%d\t%127[^\n]", filename, &seconds, title) >= 2) {
            if (title[0] == '\0') {
                snprintf(title, sizeof(title), "メモ%d", count + 1);
            }
        } else if (sscanf(line, "%63s %d", filename, &seconds) == 2) {
            snprintf(title, sizeof(title), "メモ%d", count + 1);
        } else {
            continue;
        }

        if (fileExists(filename)) {
            snprintf(memoFiles[count], sizeof(memoFiles[count]), "%s", filename);
            memoSeconds[count] = seconds;
            snprintf(memoTitles[count], TITLE_MAX, "%s", title);
            count++;
        }
    }

    fclose(fp);
    return count;
}

void deleteSelectedMemo(
    int selectedMemo,
    char memoFiles[MAX_MEMOS][64],
    int memoSeconds[MAX_MEMOS],
    char memoTitles[MAX_MEMOS][TITLE_MAX],
    int *memoCount
) {
    if (selectedMemo < 0 || selectedMemo >= *memoCount) {
        return;
    }

    remove(memoFiles[selectedMemo]);

    for (int i = selectedMemo; i < *memoCount - 1; i++) {
        snprintf(memoFiles[i], sizeof(memoFiles[i]), "%s", memoFiles[i + 1]);
        memoSeconds[i] = memoSeconds[i + 1];
        snprintf(memoTitles[i], TITLE_MAX, "%s", memoTitles[i + 1]);
    }

    (*memoCount)--;
}

void clearAudioBuffer(AudioBuffer *buffer) {
    buffer->length = 0;
}

void audioCaptureCallback(void *userdata, Uint8 *stream, int len) {
    AudioBuffer *buffer = (AudioBuffer *)userdata;

    if (buffer->length >= buffer->maxBytes) {
        return;
    }

    if (buffer->length + len > buffer->maxBytes) {
        len = buffer->maxBytes - buffer->length;
    }

    if (buffer->length + len > buffer->capacity) {
        Uint32 newCapacity = buffer->capacity == 0 ? 4096 : buffer->capacity * 2;

        while (newCapacity < buffer->length + len) {
            newCapacity *= 2;
        }

        Uint8 *newData = (Uint8 *)SDL_realloc(buffer->data, newCapacity);
        if (newData == NULL) {
            printf("録音メモリ確保エラー\n");
            return;
        }

        buffer->data = newData;
        buffer->capacity = newCapacity;
    }

    SDL_memcpy(buffer->data + buffer->length, stream, len);
    buffer->length += len;
}

void drawText(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y) {
    SDL_Color white = {255, 255, 255, 255};

    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, white);
    if (surface == NULL) {
        printf("文字描画エラー: %s\n", TTF_GetError());
        return;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == NULL) {
        printf("テクスチャ作成エラー: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }

    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &dst);

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

bool isInsideRect(int x, int y, SDL_Rect rect) {
    return x >= rect.x &&
           x <= rect.x + rect.w &&
           y >= rect.y &&
           y <= rect.y + rect.h;
}

bool isInsideButton(int x, int y, Button button) {
    return isInsideRect(x, y, button.rect);
}

int getClickedMemoIndex(int x, int y, int memoCount) {
    if (x < 110 || x > 700) {
        return -1;
    }

    for (int i = 0; i < memoCount; i++) {
        int memoY = 190 + i * 38;

        if (y >= memoY && y <= memoY + 32) {
            return i;
        }
    }

    return -1;
}

void drawButton(SDL_Renderer *renderer, TTF_Font *font, Button button, bool active) {
    if (active) {
        SDL_SetRenderDrawColor(renderer, 120, 80, 80, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    }

    SDL_RenderFillRect(renderer, &button.rect);

    SDL_SetRenderDrawColor(renderer, 180, 180, 180, 255);
    SDL_RenderDrawRect(renderer, &button.rect);

    int textW = 0;
    int textH = 0;
    TTF_SizeUTF8(font, button.label, &textW, &textH);

    int textX = button.rect.x + (button.rect.w - textW) / 2;
    int textY = button.rect.y + (button.rect.h - textH) / 2;

    drawText(renderer, font, button.label, textX, textY);
}

bool playWavFile(const char *filename, SDL_AudioDeviceID *playDevice) {
    if (*playDevice != 0) {
        SDL_CloseAudioDevice(*playDevice);
        *playDevice = 0;
    }

    SDL_AudioSpec wavSpec;
    Uint8 *wavBuffer = NULL;
    Uint32 wavLength = 0;

    if (SDL_LoadWAV(filename, &wavSpec, &wavBuffer, &wavLength) == NULL) {
        printf("WAV読み込みエラー: %s\n", SDL_GetError());
        return false;
    }

    if (wavSpec.format == AUDIO_S16LSB) {
        Sint16 *samples = (Sint16 *)wavBuffer;
        int sampleCount = wavLength / 2;

        for (int i = 0; i < sampleCount; i++) {
            int boosted = (int)(samples[i] * PLAYBACK_GAIN);
            samples[i] = clampSample(boosted);
        }
    }

    /* アプリ内再生が小さくなる原因対策:
       デバイス側で形式変更されると音量や再生が不安定になることがあるため、
       recorded.wav / audio*.wav と同じ形式で開く。 */
    *playDevice = SDL_OpenAudioDevice(NULL, SDL_FALSE, &wavSpec, NULL, 0);

    if (*playDevice == 0) {
        printf("再生デバイスエラー: %s\n", SDL_GetError());
        SDL_FreeWAV(wavBuffer);
        return false;
    }

    SDL_QueueAudio(*playDevice, wavBuffer, wavLength);
    SDL_PauseAudioDevice(*playDevice, 0);

    SDL_FreeWAV(wavBuffer);
    return true;
}

int main(int argc, char *argv[]) {
    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) < 0) {
        printf("SDL初期化エラー: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

    if (TTF_Init() < 0) {
        printf("TTF初期化エラー: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "KIKO - 聞くメモ帳",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    if (window == NULL) {
        printf("ウィンドウ作成エラー: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        printf("レンダラー作成エラー: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    TTF_Font *font = TTF_OpenFont("NotoSansJP-Regular.ttf", 20);

    if (font == NULL) {
        font = TTF_OpenFont("C:/Windows/Fonts/meiryo.ttc", 20);
    }

    if (font == NULL) {
        printf("フォント読み込みエラー: %s\n", TTF_GetError());
        printf("NotoSansJP-Regular.ttf を main.c と同じフォルダに置いてください。\n");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    Button recordButton = {{70, 540, 150, 50}, "録音開始"};
    Button playButton   = {{250, 540, 120, 50}, "再生"};
    Button postButton   = {{400, 540, 120, 50}, "投稿"};
    Button deleteButton = {{550, 540, 150, 50}, "削除"};

    Button titleClassButton = {{210, 435, 110, 32}, "授業メモ"};
    Button titleIdeaButton  = {{335, 435, 90, 32}, "アイデア"};
    Button titleTaskButton  = {{440, 435, 90, 32}, "課題"};
    Button titleOtherButton = {{545, 435, 90, 32}, "その他"};
    Button titleClearButton = {{650, 435, 60, 32}, "消去"};

    Button effectNormalButton = {{210, 472, 90, 30}, "通常"};
    Button effectHighButton   = {{315, 472, 90, 30}, "高音"};
    Button effectLowButton    = {{420, 472, 90, 30}, "低音"};
    Button effectEchoButton   = {{525, 472, 90, 30}, "エコー"};

    SDL_Rect titleBox = {210, 395, 500, 36};

    bool running = true;
    bool recording = false;
    bool playActive = false;
    bool postActive = false;
    bool deleteActive = false;
    bool hasRecorded = false;
    bool titleEditing = false;

    EffectMode effectMode = EFFECT_NORMAL;

    Uint32 recordStartTime = 0;
    int recordSeconds = 0;
    int lastRecordSeconds = 0;

    int memoCount = 0;
    int selectedMemo = -1;
    int memoSeconds[MAX_MEMOS] = {0};
    char memoFiles[MAX_MEMOS][64];
    char memoTitles[MAX_MEMOS][TITLE_MAX];
    char currentTitle[TITLE_MAX] = "";

    memoCount = loadMemoList(memoFiles, memoSeconds, memoTitles);
    if (memoCount > 0) {
        selectedMemo = 0;
    }

    SDL_AudioDeviceID captureDevice = 0;
    SDL_AudioDeviceID playDevice = 0;

    char message[128] = "日本語は定型ボタンかCtrl+V貼り付けが安全です";
    if (memoCount > 0) {
        snprintf(message, sizeof(message), "保存済みメモを読み込みました");
    }

    SDL_Event event;

    while (running) {
        playActive = false;
        postActive = false;
        deleteActive = false;

        if (playDevice != 0 && SDL_GetQueuedAudioSize(playDevice) == 0) {
            SDL_CloseAudioDevice(playDevice);
            playDevice = 0;
        }

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }

            if (event.type == SDL_TEXTINPUT && titleEditing) {
                appendTitleText(currentTitle, event.text.text);
            }

            if (event.type == SDL_KEYDOWN && titleEditing) {
                if ((event.key.keysym.mod & KMOD_CTRL) && event.key.keysym.sym == SDLK_v) {
                    char *clipText = SDL_GetClipboardText();

                    if (clipText != NULL) {
                        appendTitleText(currentTitle, clipText);
                        SDL_free(clipText);
                        snprintf(message, sizeof(message), "貼り付けました");
                    }
                } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
                    removeLastUtf8Char(currentTitle);
                } else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {
                    titleEditing = false;
                    SDL_StopTextInput();
                    snprintf(message, sizeof(message), "タイトルを確定しました");
                } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                    titleEditing = false;
                    SDL_StopTextInput();
                    snprintf(message, sizeof(message), "タイトル入力を終了しました");
                }
            }

            if (event.type == SDL_MOUSEBUTTONDOWN) {
                int mouseX = event.button.x;
                int mouseY = event.button.y;

                if (isInsideRect(mouseX, mouseY, titleBox)) {
                    titleEditing = true;
                    SDL_StartTextInput();
                    SDL_SetTextInputRect(&titleBox);
                    snprintf(message, sizeof(message), "タイトル入力中。Ctrl+Vも使えます");
                } else if (isInsideButton(mouseX, mouseY, effectNormalButton)) {
                    effectMode = EFFECT_NORMAL;
                    snprintf(message, sizeof(message), "音声効果を通常にしました");
                } else if (isInsideButton(mouseX, mouseY, effectHighButton)) {
                    effectMode = EFFECT_HIGH;
                    snprintf(message, sizeof(message), "音声効果を高音にしました");
                } else if (isInsideButton(mouseX, mouseY, effectLowButton)) {
                    effectMode = EFFECT_LOW;
                    snprintf(message, sizeof(message), "音声効果を低音にしました");
                } else if (isInsideButton(mouseX, mouseY, effectEchoButton)) {
                    effectMode = EFFECT_ECHO;
                    snprintf(message, sizeof(message), "音声効果をエコーにしました");
                } else if (isInsideButton(mouseX, mouseY, titleClassButton)) {
                    snprintf(currentTitle, sizeof(currentTitle), "授業メモ");
                    snprintf(message, sizeof(message), "タイトルを授業メモにしました");
                } else if (isInsideButton(mouseX, mouseY, titleIdeaButton)) {
                    snprintf(currentTitle, sizeof(currentTitle), "アイデア");
                    snprintf(message, sizeof(message), "タイトルをアイデアにしました");
                } else if (isInsideButton(mouseX, mouseY, titleTaskButton)) {
                    snprintf(currentTitle, sizeof(currentTitle), "課題");
                    snprintf(message, sizeof(message), "タイトルを課題にしました");
                } else if (isInsideButton(mouseX, mouseY, titleOtherButton)) {
                    snprintf(currentTitle, sizeof(currentTitle), "その他");
                    snprintf(message, sizeof(message), "タイトルをその他にしました");
                } else if (isInsideButton(mouseX, mouseY, titleClearButton)) {
                    currentTitle[0] = '\0';
                    snprintf(message, sizeof(message), "タイトルを消去しました");
                } else {
                    if (titleEditing) {
                        titleEditing = false;
                        SDL_StopTextInput();
                    }

                    if (isInsideButton(mouseX, mouseY, recordButton)) {
                        if (!recording) {
                            SDL_AudioSpec desired;
                            SDL_AudioSpec obtained;

                            SDL_zero(desired);
                            desired.freq = 44100;
                            desired.format = AUDIO_S16LSB;
                            desired.channels = 1;
                            desired.samples = 4096;
                            desired.callback = audioCaptureCallback;
                            desired.userdata = &audioBuffer;

                            captureDevice = SDL_OpenAudioDevice(NULL, SDL_TRUE, &desired, &obtained, 0);

                            if (captureDevice == 0) {
                                snprintf(message, sizeof(message), "録音デバイスを開けません");
                                printf("録音デバイスエラー: %s\n", SDL_GetError());
                            } else {
                                clearAudioBuffer(&audioBuffer);
                                audioBuffer.spec = obtained;
                                audioBuffer.maxBytes = obtained.freq * obtained.channels * 2 * MAX_RECORD_SECONDS;

                                recordSeconds = 0;
                                recordStartTime = SDL_GetTicks();
                                recording = true;
                                hasRecorded = false;
                                recordButton.label = "録音停止";

                                SDL_PauseAudioDevice(captureDevice, 0);

                                snprintf(message, sizeof(message), "録音を開始しました");
                            }
                        } else {
                            SDL_PauseAudioDevice(captureDevice, 1);
                            SDL_CloseAudioDevice(captureDevice);
                            captureDevice = 0;

                            recording = false;
                            recordButton.label = "録音開始";
                            lastRecordSeconds = recordSeconds;

                            if (saveWavFile("recorded.wav", &audioBuffer)) {
                                hasRecorded = true;
                                snprintf(message, sizeof(message), "録音を保存しました。投稿できます");
                            } else {
                                hasRecorded = false;
                                snprintf(message, sizeof(message), "録音データがありません");
                            }
                        }
                    } else if (isInsideButton(mouseX, mouseY, playButton)) {
                        playActive = true;

                        if (hasRecorded) {
                            if (saveWavFileWithEffect("preview.wav", &audioBuffer, effectMode) &&
                                playWavFile("preview.wav", &playDevice)) {
                                snprintf(message, sizeof(message), "%s加工でプレビュー中です", getEffectName(effectMode));
                            } else {
                                snprintf(message, sizeof(message), "再生できませんでした");
                            }
                        } else if (selectedMemo >= 0) {
                            if (playWavFile(memoFiles[selectedMemo], &playDevice)) {
                                snprintf(message, sizeof(message), "メモ%dを再生中です", selectedMemo + 1);
                            } else {
                                snprintf(message, sizeof(message), "メモを再生できませんでした");
                            }
                        } else {
                            snprintf(message, sizeof(message), "再生するメモを選んでください");
                        }
                    } else if (isInsideButton(mouseX, mouseY, postButton)) {
                        postActive = true;

                        if (!hasRecorded) {
                            snprintf(message, sizeof(message), "投稿する録音がありません");
                        } else if (memoCount >= MAX_MEMOS) {
                            snprintf(message, sizeof(message), "メモ一覧がいっぱいです");
                        } else {
                            if (!createNextMemoFilename(memoFiles[memoCount], sizeof(memoFiles[memoCount]))) {
                                snprintf(message, sizeof(message), "保存ファイル名を作れません");
                            } else if (saveWavFileWithEffect(memoFiles[memoCount], &audioBuffer, effectMode)) {
                                if (currentTitle[0] == '\0') {
                                    snprintf(memoTitles[memoCount], TITLE_MAX, "無題メモ");
                                } else {
                                    snprintf(memoTitles[memoCount], TITLE_MAX, "%s", currentTitle);
                                }

                                memoSeconds[memoCount] = lastRecordSeconds;
                                selectedMemo = memoCount;
                                memoCount++;
                                hasRecorded = false;
                                currentTitle[0] = '\0';

                                saveMemoList(memoFiles, memoSeconds, memoTitles, memoCount);

                                snprintf(message, sizeof(message), "%s加工で投稿しました", getEffectName(effectMode));
                            } else {
                                snprintf(message, sizeof(message), "投稿に失敗しました");
                            }
                        }
                    } else if (isInsideButton(mouseX, mouseY, deleteButton)) {
                        deleteActive = true;

                        if (recording) {
                            snprintf(message, sizeof(message), "録音中は削除できません");
                        } else if (selectedMemo < 0 || memoCount == 0) {
                            snprintf(message, sizeof(message), "削除するメモを選んでください");
                        } else {
                            int deletedNumber = selectedMemo + 1;

                            if (playDevice != 0) {
                                SDL_CloseAudioDevice(playDevice);
                                playDevice = 0;
                            }

                            deleteSelectedMemo(selectedMemo, memoFiles, memoSeconds, memoTitles, &memoCount);
                            saveMemoList(memoFiles, memoSeconds, memoTitles, memoCount);

                            hasRecorded = false;

                            if (memoCount == 0) {
                                selectedMemo = -1;
                            } else if (selectedMemo >= memoCount) {
                                selectedMemo = memoCount - 1;
                            }

                            snprintf(message, sizeof(message), "メモ%dを削除しました", deletedNumber);
                        }
                    } else {
                        int clickedMemo = getClickedMemoIndex(mouseX, mouseY, memoCount);

                        if (clickedMemo >= 0) {
                            selectedMemo = clickedMemo;
                            hasRecorded = false;
                            snprintf(message, sizeof(message), "メモ%dを選択しました", selectedMemo + 1);
                        } else {
                            snprintf(message, sizeof(message), "ボタン以外がクリックされました");
                        }
                    }
                }
            }
        }

        if (recording) {
            recordSeconds = (SDL_GetTicks() - recordStartTime) / 1000;

            if (recordSeconds >= MAX_RECORD_SECONDS) {
                SDL_PauseAudioDevice(captureDevice, 1);
                SDL_CloseAudioDevice(captureDevice);
                captureDevice = 0;

                recording = false;
                recordButton.label = "録音開始";
                lastRecordSeconds = recordSeconds;

                if (saveWavFile("recorded.wav", &audioBuffer)) {
                    hasRecorded = true;
                    snprintf(message, sizeof(message), "最大300秒で自動保存しました");
                } else {
                    hasRecorded = false;
                    snprintf(message, sizeof(message), "録音データがありません");
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 25, 25, 30, 255);
        SDL_RenderClear(renderer);

        drawText(renderer, font, "KIKO", 365, 25);
        drawText(renderer, font, "聞くメモ帳", 330, 60);

        SDL_Rect board = {80, 115, 640, 260};
        SDL_SetRenderDrawColor(renderer, 55, 55, 65, 255);
        SDL_RenderFillRect(renderer, &board);
        SDL_SetRenderDrawColor(renderer, 160, 160, 160, 255);
        SDL_RenderDrawRect(renderer, &board);

        drawText(renderer, font, "音声メモ一覧", 110, 135);

        if (memoCount == 0) {
            drawText(renderer, font, "まだ投稿されたメモはありません", 120, 200);
        } else {
            for (int i = 0; i < memoCount; i++) {
                char memoText[256];

                if (i == selectedMemo) {
                    snprintf(
                        memoText,
                        sizeof(memoText),
                        "> %s  %02d:%02d  %s",
                        memoTitles[i],
                        memoSeconds[i] / 60,
                        memoSeconds[i] % 60,
                        memoFiles[i]
                    );
                } else {
                    snprintf(
                        memoText,
                        sizeof(memoText),
                        "・%s  %02d:%02d  %s",
                        memoTitles[i],
                        memoSeconds[i] / 60,
                        memoSeconds[i] % 60,
                        memoFiles[i]
                    );
                }

                drawText(renderer, font, memoText, 120, 190 + i * 38);
            }
        }

        drawText(renderer, font, "タイトル", 110, 400);

        if (titleEditing) {
            SDL_SetRenderDrawColor(renderer, 90, 90, 120, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 65, 65, 75, 255);
        }

        SDL_RenderFillRect(renderer, &titleBox);
        SDL_SetRenderDrawColor(renderer, 170, 170, 170, 255);
        SDL_RenderDrawRect(renderer, &titleBox);

        char titleDisplay[TITLE_MAX + 4];

        if (currentTitle[0] == '\0') {
            snprintf(titleDisplay, sizeof(titleDisplay), "ここにタイトルを入力");
        } else if (titleEditing) {
            snprintf(titleDisplay, sizeof(titleDisplay), "%s_", currentTitle);
        } else {
            snprintf(titleDisplay, sizeof(titleDisplay), "%s", currentTitle);
        }

        drawText(renderer, font, titleDisplay, titleBox.x + 10, titleBox.y + 5);

        drawButton(renderer, font, titleClassButton, false);
        drawButton(renderer, font, titleIdeaButton, false);
        drawButton(renderer, font, titleTaskButton, false);
        drawButton(renderer, font, titleOtherButton, false);
        drawButton(renderer, font, titleClearButton, false);

        drawText(renderer, font, "効果", 110, 475);
        drawButton(renderer, font, effectNormalButton, effectMode == EFFECT_NORMAL);
        drawButton(renderer, font, effectHighButton, effectMode == EFFECT_HIGH);
        drawButton(renderer, font, effectLowButton, effectMode == EFFECT_LOW);
        drawButton(renderer, font, effectEchoButton, effectMode == EFFECT_ECHO);

        char timeText[64];
        snprintf(
            timeText,
            sizeof(timeText),
            "録音時間 %02d:%02d",
            recordSeconds / 60,
            recordSeconds % 60
        );
        drawText(renderer, font, timeText, 110, 508);

        drawText(renderer, font, message, 310, 508);

        drawButton(renderer, font, recordButton, recording);
        drawButton(renderer, font, playButton, playActive);
        drawButton(renderer, font, postButton, postActive);
        drawButton(renderer, font, deleteButton, deleteActive);

        SDL_RenderPresent(renderer);
    }

    if (titleEditing) {
        SDL_StopTextInput();
    }

    if (recording && captureDevice != 0) {
        SDL_CloseAudioDevice(captureDevice);
    }

    if (playDevice != 0) {
        SDL_CloseAudioDevice(playDevice);
    }

    SDL_free(audioBuffer.data);

    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    TTF_Quit();
    SDL_Quit();

    return 0;
}
