//
//  main.c
//  TImeTable
//
//  Created by Js Na on 6/18/25.
//

#include <stdio.h>
#include <string.h>

#include <curl/curl.h> // 웹 요청을 위한 CURL 라이브러리

char* getURL(char *url) { // CURL을 이용해 HTTP 요청(GET)
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (curl == NULL) {
        return NULL; // CURL 초기화 실패
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // 리다이렉션 허용
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL); // 응답을 처리하지 않음
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return NULL; // 요청 실패
    }
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code != 200) {
        fprintf(stderr, "HTTP 요청 실패: %ld\n", response_code);
        curl_easy_cleanup(curl);
        return NULL; // HTTP 요청 실패
    }
    // 응답 리턴
    char *response = NULL;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &response);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return response;
}

char* searchNEISSchool(char *schoolName) { // NEIS OpenAPI를 이용해 학교 검색
    
    return "";
}
char* getNEISTimeTable(void) { // NEIS OpenAPI를 이용해 시간표를 가져오기
    
    return "";
}

int parseCSVHeader(FILE *file, char *searchHeader) { // CSV에서 헤더를 찾아 해당 열의 인덱스를 반환
    char header[1024];

    // 파일 포인터를 파일의 시작으로 되돌립니다.
    // 이렇게 해야 함수를 여러 번 호출해도 항상 헤더 줄을 읽을 수 있습니다.
    fseek(file, 0, SEEK_SET);

    if (fgets(header, sizeof(header), file) != NULL) {
        // 개행 문자 제거
        header[strcspn(header, "\n")] = 0;

        // BOM(Byte Order Mark) 제거 (UTF-8 with BOM)
        // CSV 파일이 UTF-8 BOM으로 시작하는 경우 첫 3바이트를 건너뜁니다.
        if ( (unsigned char)header[0] == 0xEF &&
             (unsigned char)header[1] == 0xBB &&
             (unsigned char)header[2] == 0xBF ) {
            memmove(header, header + 3, strlen(header) - 3 + 1); // +1 for null terminator
        }

        char *token;
        int index = 0;
        // 이제 원본 header 문자열을 다시 토큰화하여 검색 시작
        token = strtok(header, ","); // 주의: strtok은 첫 호출 시 문자열을 변경하므로,
                                     // 이전 print_token이 사용한 복사본(temp_header_for_print)과 별개로
                                     // 원본 header를 여기서 사용해도 됩니다.

        while (token != NULL) {
            // 양 끝 공백 제거 (trim) - 필요한 경우 추가
            // char *trimmed_token = token;
            // while(*trimmed_token == ' ') trimmed_token++;
            // char *end = trimmed_token + strlen(trimmed_token) - 1;
            // while(end > trimmed_token && *end == ' ') end--;
            // *(end + 1) = '\0';

            if (strcmp(token, searchHeader) == 0) {
                return index;
            }
            token = strtok(NULL, ",");
            index++;
        }
        printf("'%s' 헤더를 찾을 수 없습니다.\n", searchHeader);
        return -1;
    } else {
        printf("파일에서 헤더를 읽을 수 없습니다.\n");
        return -1;
    }
}


int main(int argc, const char * argv[]) {
    // insert code here...
    printf("##########\n");
    printf("TimeTable\n");
    printf("by Js Na\n");
    printf("##########\n");
    
    printf("NEIS 교육정보개방포털에서 다운로드한 .CSV 파일이 필요합니다.\n"); // https://open.neis.go.kr/portal/data/service/selectServicePage.do?infId=OPEN18620200826103326268120&infSeq=1 에서 다운로드한 CSV 파일 필요.
    printf("파일명을 입력해 주세요.\n");
    
    char fileName[256] = "고등학교시간표.csv";
    
    scanf("%s", fileName);
    FILE *file = fopen(fileName, "r");
    if (file == NULL) {
        printf("파일을 열 수 없습니다.\n");
        return 1;
    }
    int dateCol = parseCSVHeader(file, "시간표일자");
    int gradeCol = parseCSVHeader(file, "학년");
    int classCol = parseCSVHeader(file, "강의실명");
    int timeCol = parseCSVHeader(file, "교시");
    int subjectCol = parseCSVHeader(file, "수업내용");
    
    if (dateCol < 0 || gradeCol < 0 || classCol < 0 || timeCol < 0 || subjectCol < 0) {
        printf("필요한 헤더를 찾을 수 없습니다. 프로그램을 종료합니다.\n");
        fclose(file);
        return 1;
    }
    printf("파일을 성공적으로 열었습니다.\n");
    printf("학년을 입력해 주세요: ");
    int grade, class;
    scanf("%d", &grade);
    printf("반을 입력해 주세요: ");
    scanf("%d", &class);
    printf("\n\n\n");
    
    // CSV파일 row 반복
    char line[1024];
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\n")] = 0;
        char *token;
        int colIndex = 0;
        token = strtok(line, ",");
        
        char printfLine[1024] = ""; // printfLine에 출력할 내용을 저장
        while (token != NULL) {
            if (colIndex == dateCol) {
                strcat(printfLine, "\n날짜:");
            } else if (colIndex == gradeCol) {
                strcat(printfLine, "\n학년:");
            } else if (colIndex == classCol) {
                strcat(printfLine, "\n강의실:");
            } else if (colIndex == timeCol) {
                strcat(printfLine, "\n교시:");
            } else if (colIndex == subjectCol) {
                strcat(printfLine, "\n수업내용:");
            } else {
                token = strtok(NULL, ",");
                colIndex++;
                continue; // skip other columns
            }
            strcat(printfLine, token);

            token = strtok(NULL, ",");
            colIndex++;
        }
        printf("%s\n", printfLine); // 최종 출력
    }
    fclose(file);
    return 0;
}
