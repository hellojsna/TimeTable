//
//  main.c
//  TimeTable
//
//  Created by Js Na on 6/18/25.
//

#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h> // close
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> // gethostbyname
#include <time.h>
#include <ctype.h>
#include <locale.h>
#include <wchar.h>

#include "parson.h" // JSON 파싱 라이브러리


#define RECV_BUFFER_SIZE 8192 // 서버 응답을 받을 버퍼 크기
// 학교 정보 구조체 정의
typedef struct {
    char edu_code[16];    // 교육청 코드 (ATPT_OFCDC_SC_CODE)
    char school_code[32]; // 학교 코드 (SD_SCHUL_CODE)
    char school_name[128]; // 학교 이름 (SCHUL_NM)
    char address[256]; // 학교 주소
} SchoolInfo;

#define MAX_SEARCH_RESULTS 100 // 최대 학교 검색 결과 수

// 전역 변수 선언 (프로그램 전체에서 접근 가능)
char g_selected_edu_code[16] = "";    // 선택된 교육청 코드
char g_selected_school_code[32] = ""; // 선택된 학교 코드
char g_selected_school_name[128] = ""; // 선택된 학교 이름

#define MAX_SCHOOLS 100
#define MAX_FIELD_LEN 128 // 각 필드의 최대 길이

void trim_whitespace(char *str) {
    if (str == NULL) return;
    
    // Trim leading whitespace
    char *start = str;
    while (*start && (isspace((unsigned char)*start) || (unsigned char)*start == 0xEF || (unsigned char)*start == 0xBB || (unsigned char)*start == 0xBF)) {
        // isspace()로 공백문자 제거, 0xEF, 0xBB, 0xBF는 UTF-8 BOM
        start++;
    }
    
    // Trim trailing whitespace
    char *end = str + strlen(str) - 1;
    while (end >= start && (isspace((unsigned char)*end) || (unsigned char)*end == 0xEF || (unsigned char)*end == 0xBB || (unsigned char)*end == 0xBF)) {
        end--;
    }
    *(end + 1) = '\0'; // Null-terminate the string
    
    // Shift the string if leading whitespace was trimmed
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

char* getURL(char *hostname, char *path, int port) {
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char request_buffer[RECV_BUFFER_SIZE * 2]; // 요청 버퍼는 응답 버퍼보다 크게 잡을 수 있음
    char temp_response_buffer[RECV_BUFFER_SIZE];
    char *response_body = NULL;
    size_t total_received_size = 0;
    const char *user_agent_string = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/137.0.0.0 Safari/537.36"; // 사용자 에이전트 문자열
    
    // 1. 소켓 생성
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        return NULL;
    }
    
    // 2. 호스트 이름 해결 (DNS 룩업)
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host: %s\n", hostname);
        close(sockfd);
        return NULL;
    }
    
    // 3. 서버 주소 설정
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(port); // 호스트 바이트 순서를 네트워크 바이트 순서로 변환
    
    // 4. 서버 연결
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        close(sockfd);
        return NULL;
    }
    
    // 5. HTTP GET 요청 문자열 구성 및 전송
    // User-Agent 헤더 포함
    sprintf(request_buffer,
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: %s\r\n" // User-Agent 헤더 추가
            "Connection: close\r\n" // 응답 후 연결 닫기 요청
            "\r\n", // 헤더 종료 (빈 줄)
            path, hostname, user_agent_string);
    
    printf("Sending request:\n%s", request_buffer);
    
    if (send(sockfd, request_buffer, strlen(request_buffer), 0) < 0) {
        perror("ERROR writing to socket");
        close(sockfd);
        return NULL;
    }
    
    // 6. 서버 응답 수신 및 본문 추출
    int bytes_received;
    char *header_end = NULL;
    int header_processed = 0;
    
    while ((bytes_received = recv(sockfd, temp_response_buffer, RECV_BUFFER_SIZE - 1, 0)) > 0) {
        temp_response_buffer[bytes_received] = '\0'; // 널 종료
        
        if (!header_processed) {
            // 헤더와 본문 구분자 "\r\n\r\n" 찾기
            header_end = strstr(temp_response_buffer, "\r\n\r\n");
            
            if (header_end) {
                // 헤더 끝 지점으로부터 본문 시작 위치 계산
                size_t body_start_offset = (header_end - temp_response_buffer) + 4; // "\r\n\r\n" 길이 포함
                
                // 본문 데이터 크기
                size_t body_chunk_size = bytes_received - body_start_offset;
                
                // 응답 본문 저장 공간 초기 할당
                response_body = (char *)malloc(body_chunk_size + 1);
                if (response_body == NULL) {
                    perror("ERROR malloc failed");
                    close(sockfd);
                    return NULL;
                }
                memcpy(response_body, temp_response_buffer + body_start_offset, body_chunk_size);
                response_body[body_chunk_size] = '\0';
                total_received_size = body_chunk_size;
                header_processed = 1;
            } else {
                // 아직 헤더 끝을 찾지 못했거나, 헤더만 있는 경우
                // 다음 recv에서 나머지 부분을 받을 수 있음. 현재는 아무것도 저장 안함.
                // NOTE: 실제로는 헤더가 RECV_BUFFER_SIZE보다 클 수도 있으므로,
                // 이를 처리하려면 더 복잡한 로직(버퍼 재할당, 헤더 누적)이 필요합니다.
                // 이 예시에서는 간단화를 위해 첫 청크에 헤더 끝이 있다고 가정합니다.
            }
        } else {
            // 헤더가 이미 처리되었으므로, 모든 수신 데이터는 본문으로 간주
            response_body = (char *)realloc(response_body, total_received_size + bytes_received + 1);
            if (response_body == NULL) {
                perror("ERROR realloc failed");
                close(sockfd);
                return NULL;
            }
            memcpy(response_body + total_received_size, temp_response_buffer, bytes_received);
            response_body[total_received_size + bytes_received] = '\0';
            total_received_size += bytes_received;
        }
    }
    
    if (bytes_received < 0) {
        perror("ERROR reading from socket");
        if (response_body) free(response_body); // 에러 발생 시 할당된 메모리 해제
        close(sockfd);
        return NULL;
    }
    
    // 7. 소켓 닫기
    close(sockfd);
    
    return response_body;
}

int searchNEISSchool(char *schoolName) {
    char full_path[256];
    // strcat은 첫 번째 인자(대상)에 두 번째 인자(원본)를 이어붙이고 첫 번째 인자를 반환합니다.
    // 따라서 안전하게 사용하려면 충분한 버퍼를 먼저 만들고 sprintf 등을 사용해야 합니다.
    // 여기서는 동적 프록시 경로와 쿼리 파라미터를 조합합니다.
    sprintf(full_path, "/https://hello.jsna.dev/lunch/api/search-hakgyo.php?sc=%s", schoolName);
    
    char* json_response = getURL("cors-proxy.jsna.workers.dev", full_path, 80);
    
    if (json_response == NULL) {
        fprintf(stderr, "잘못된 응답\n");
        return -1; // 검색 실패
    }
    
    printf("\nJSON 응답:\n%s\n", json_response);
    
    SchoolInfo schools[MAX_SEARCH_RESULTS];
    int num_schools = 0;
    
    // JSON 파싱 시작
    JSON_Value *root_value = json_parse_string(json_response);
    if (root_value == NULL) {
        fprintf(stderr, "ERROR: JSON 파싱 실패\n");
        free(json_response);
        return -1;
    }
    
    JSON_Object *root_object = json_value_get_object(root_value);
    if (root_object == NULL) {
        fprintf(stderr, "ERROR: 잘못된 형식\n");
        json_value_free(root_value);
        free(json_response);
        return -1;
    }
    
    JSON_Array *school_info_array = json_object_get_array(root_object, "schoolInfo");
    if (school_info_array == NULL) {
        fprintf(stderr, "ERROR: 'schoolInfo' 찾기 실패\n");
        json_value_free(root_value);
        free(json_response);
        return -1;
    }
    
    // "schoolInfo" 배열 내에서 'row' 배열을 포함하는 객체를 찾습니다.
    // 이 예시에서는 'head'와 'row'가 각각의 객체로 배열 내에 존재하므로,
    // 배열을 순회하며 "row" 키를 가진 객체를 찾아야 합니다.
    JSON_Array *row_array = NULL;
    for (size_t i = 0; i < json_array_get_count(school_info_array); ++i) {
        JSON_Object *current_obj_in_school_info = json_array_get_object(school_info_array, i);
        if (current_obj_in_school_info != NULL) {
            row_array = json_object_get_array(current_obj_in_school_info, "row");
            if (row_array != NULL) {
                break; // 'row' 배열을 찾았으므로 루프 종료
            }
        }
    }
    
    if (row_array == NULL) {
        fprintf(stderr, "ERROR: 'schoolInfo'에서 'row' 찾기 실패.\n");
        json_value_free(root_value);
        free(json_response);
        return -1;
    }
    
    // "row" 배열 순회하며 각 학교 정보 추출
    size_t i;
    for (i = 0; i < json_array_get_count(row_array) && num_schools < MAX_SEARCH_RESULTS; ++i) {
        JSON_Object *school_item = json_array_get_object(row_array, i);
        if (school_item == NULL) continue;
        
        const char *edu_code_str = json_object_get_string(school_item, "ATPT_OFCDC_SC_CODE");
        const char *school_code_str = json_object_get_string(school_item, "SD_SCHUL_CODE");
        const char *school_name_str = json_object_get_string(school_item, "SCHUL_NM");
        const char *address_main_str = json_object_get_string(school_item, "ORG_RDNMA"); // 학교 주소
        const char *address_detail_str = json_object_get_string(school_item, "ORG_RDNDA"); // 상세 주소
        
        if (edu_code_str && school_code_str && school_name_str) {
            strncpy(schools[num_schools].edu_code, edu_code_str, sizeof(schools[num_schools].edu_code) - 1);
            schools[num_schools].edu_code[sizeof(schools[num_schools].edu_code) - 1] = '\0';
            
            strncpy(schools[num_schools].school_code, school_code_str, sizeof(schools[num_schools].school_code) - 1);
            schools[num_schools].school_code[sizeof(schools[num_schools].school_code) - 1] = '\0';
            
            strncpy(schools[num_schools].school_name, school_name_str, sizeof(schools[num_schools].school_name) - 1);
            schools[num_schools].school_name[sizeof(schools[num_schools].school_name) - 1] = '\0';
            // 주소 필드 조합 및 복사
            schools[num_schools].address[0] = '\0'; // 초기화
            
            if (address_main_str) {
                strncat(schools[num_schools].address, address_main_str, sizeof(schools[num_schools].address) - 1);
                schools[num_schools].address[sizeof(schools[num_schools].address) - 1] = '\0';
            }
            if (address_detail_str) {
                // 이미 주소 메인이 있으면 공백 추가
                if (schools[num_schools].address[0] != '\0' && strlen(schools[num_schools].address) < sizeof(schools[num_schools].address) - 1) {
                    strncat(schools[num_schools].address, " ", sizeof(schools[num_schools].address) - strlen(schools[num_schools].address) - 1);
                }
                strncat(schools[num_schools].address, address_detail_str, sizeof(schools[num_schools].address) - strlen(schools[num_schools].address) - 1);
                schools[num_schools].address[sizeof(schools[num_schools].address) - 1] = '\0';
            }
            num_schools++;
        }
    }
    
    json_value_free(root_value); // JSON 파싱 후 메모리 해제
    free(json_response); // getURL에서 할당한 응답 본문 메모리 해제
    
    if (num_schools == 0) {
        printf("'%s' 학교를 찾을 수 없습니다.\n", schoolName);
        return -1;
    }
    
    // 사용자에게 학교 선택 요청
    printf("\n--- '%s' 검색 결과 ---\n", schoolName);
    for (int j = 0; j < num_schools; ++j) {
        printf("%d. %s (교육청: %s, 코드: %s)\n", j + 1,
               schools[j].school_name, schools[j].edu_code, schools[j].school_code);
        printf("   주소: %s\n", schools[j].address); // 주소 출력
        
    }
    
    int choice;
    printf("학교를 선택해 주세요. (1-%d 번호 입력): ", num_schools);
    if (scanf("%d", &choice) != 1 || choice < 1 || choice > num_schools) {
        printf("올바르지 않은 선택입니다.\n");
        // 유효하지 않은 선택이므로 전역 변수에 저장하지 않고 실패 반환
        // TODO: 재시도 로직 추가 가능
        return -1;
    }
    
    // 선택된 학교 정보 전역 변수에 저장
    strncpy(g_selected_edu_code, schools[choice - 1].edu_code, sizeof(g_selected_edu_code) - 1);
    g_selected_edu_code[sizeof(g_selected_edu_code) - 1] = '\0';
    
    strncpy(g_selected_school_code, schools[choice - 1].school_code, sizeof(g_selected_school_code) - 1);
    g_selected_school_code[sizeof(g_selected_school_code) - 1] = '\0';
    
    strncpy(g_selected_school_name, schools[choice - 1].school_name, sizeof(g_selected_school_name) - 1);
    // 버퍼 비우기 (scanf 후 남은 개행 문자 처리)
    while (getchar() != '\n');
    
    printf("\n선택된 학교:\n");
    printf("  교육청 코드: %s\n", g_selected_edu_code);
    printf("  학교 코드: %s\n", g_selected_school_code);
    printf("  학교 이름: %s\n", g_selected_school_name);
    
    return 0;
}

int parseCSVHeader(const char *csvString, const char *searchHeader) {
    if (csvString == NULL || *csvString == '\0') {
        printf("입력된 CSV 문자열이 비어있습니다.\n");
        return -1;
    }
    
    // strtok은 원본 문자열을 변경하므로 복사본을 생성합니다.
    char *csvCopy = strdup(csvString);
    if (csvCopy == NULL) {
        printf("메모리 할당에 실패했습니다.\n");
        return -1;
    }
    
    // 첫 번째 줄(헤더)만 추출합니다.
    char *header = strtok(csvCopy, "\n\r"); // \r (캐리지 리턴)도 처리
    
    if (header != NULL) {
        // BOM(Byte Order Mark) 제거 (UTF-8 with BOM)
        if ((unsigned char)header[0] == 0xEF &&
            (unsigned char)header[1] == 0xBB &&
            (unsigned char)header[2] == 0xBF) {
            memmove(header, header + 3, strlen(header) - 2); // strlen(header)-3+1
        }
        
        char *token;
        int index = 0;
        
        // 헤더 문자열을 ',' 기준으로 토큰화하여 검색 시작
        token = strtok(header, ",");
        
        while (token != NULL) {
            // 양 끝의 공백을 제거하는 로직이 필요하다면 여기에 추가할 수 있습니다.
            // 예: trim(token);
            
            if (strcmp(token, searchHeader) == 0) {
                free(csvCopy); // 함수 종료 전 메모리 해제
                return index;
            }
            token = strtok(NULL, ",");
            index++;
        }
        
        printf("'%s' 헤더를 찾을 수 없습니다.\n", searchHeader);
        free(csvCopy); // 함수 종료 전 메모리 해제
        return -1;
    } else {
        printf("CSV 문자열에서 헤더를 추출할 수 없습니다.\n");
        free(csvCopy); // 함수 종료 전 메모리 해제
        return -1;
    }
}

int get_weekday(const char* date_str) {
    if (strlen(date_str) != 8) {
        return -1; // 잘못된 형식
    }
    char year_s[5], month_s[3], day_s[3];
    strncpy(year_s, date_str, 4);
    year_s[4] = '\0';
    strncpy(month_s, date_str + 4, 2);
    month_s[2] = '\0';
    strncpy(day_s, date_str + 6, 2);
    day_s[2] = '\0';

    struct tm time_info = {0};
    time_info.tm_year = atoi(year_s) - 1900;
    time_info.tm_mon = atoi(month_s) - 1;
    time_info.tm_mday = atoi(day_s);
    
    // mktime을 호출하여 tm_wday (요일) 필드를 계산
    // tm_wday: 0=일요일, 1=월요일, ..., 6=토요일
    if (mktime(&time_info) == -1) {
        return -1; // 잘못된 날짜
    }

    int weekday = time_info.tm_wday;
    if (weekday >= 1 && weekday <= 5) { // 월요일(1) ~ 금요일(5)
        return weekday - 1; // 0 ~ 4 범위로 변환
    }
    
    return -1; // 주말 또는 오류
}
// 셀의 목표 너비 (화면상 차지하는 칸 수)
#define TARGET_CELL_WIDTH 16

// 문자열의 실제 화면 너비를 계산하는 함수
int get_str_width(const char *s) {
    wchar_t wstr[1024];
    // 멀티바이트 문자열(UTF-8)을 와이드 캐릭터 문자열로 변환
    int len = mbstowcs(wstr, s, 1024);
    if (len < 0) return strlen(s); // 변환 실패 시 바이트 길이 반환
    // 와이드 캐릭터 문자열의 화면 너비 계산
    return wcswidth(wstr, len);
}

// 문자열을 출력하고 너비에 맞게 공백을 추가하는 함수
void print_padded_cell(const char *subject) {
    int width = get_str_width(subject);
    // 목표 너비에서 실제 너비를 뺀 만큼 공백 추가
    int padding = TARGET_CELL_WIDTH - width;
    if (padding < 0) padding = 0; // 너비를 초과하면 공백 없음

    printf(" %s", subject);
    for (int i = 0; i < padding; i++) {
        printf(" ");
    }
    printf("|");
}

void remove_spaces_inplace(char *str) {
    if (str == NULL) return; // 널 포인터가 들어온 경우 함수 종료

    char *writer = str; // 공백이 아닌 문자를 써야 할 위치를 가리키는 포인터
    char *reader = str; // 문자열을 처음부터 읽어나갈 포인터

    // 문자열의 끝에 도달할 때까지 반복
    while (*reader != '\0') {
        // 현재 reader가 가리키는 문자가 공백이 '아니라면'
        if (*reader != ' ') {
            *writer = *reader; // writer 위치에 해당 문자를 복사
            writer++;          // writer 위치를 다음 칸으로 이동
        }
        // reader는 공백이든 아니든 항상 다음 문자로 이동
        reader++;
    }
    // 모든 작업이 끝난 후, writer가 가리키는 위치에 널 종료 문자를 추가하여
    // 새로운 문자열의 끝을 지정합니다.
    *writer = '\0';
}

int getNEISTimeTable(int grade, int class) {
    setlocale(LC_ALL, "");
    char full_path[256] = "";
    
    // 현재 날짜를 가져와 YYYYMMDD 형식으로 포맷팅
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char start_date[9];
    char end_date[9]; // NEIS API는 시작일과 종료일을 요구함. 여기서는 한 주를 기준으로.
    
    // 오늘의 날짜
    strftime(start_date, sizeof(start_date), "%Y%m%d", tm_info);
    
    // 일주일 후의 날짜 (간단하게 계산, 월말/연말 처리 필요 시 복잡해짐)
    // 여기서는 단순히 오늘부터 6일 후로 계산
    // 더 정확한 계산을 위해서는 mktime과 struct tm 조작이 필요
    tm_info->tm_mday += 6;
    mktime(tm_info); // mktime으로 tm_info를 정규화하여 월, 일 등을 업데이트
    strftime(end_date, sizeof(end_date), "%Y%m%d", tm_info);
    
    
    // sprintf를 사용하여 URL 경로를 만듭니다.
    // 날짜 매개변수 추가 (fy: from_ymd, ty: to_ymd)
    sprintf(full_path, "/https://hello.jsna.dev/timetable/get-timetable.php?oc=%s&sc=%s&gd=%d&cl=%d&fy=%s&ty=%s",
            g_selected_edu_code, g_selected_school_code, grade, class, start_date, end_date);
    //response is csv
    
    char* csv_response = getURL("cors-proxy.jsna.workers.dev", full_path, 80);
    if (csv_response == NULL) {
        fprintf(stderr, "잘못된 HTTP 응답\n");
        return -1; // 시간표 가져오기 실패
    }
    printf("시간표 데이터 분석 중...\n");

    // 앞에 있는 43fe\r\n 같은 데이터 제거: 첫번째 \r\n 찾아서 그 앞에 다 날리기
    char *start_ptr = strstr(csv_response, "\r\n");
    if (start_ptr != NULL) {
        // 첫 번째 중괄호 위치를 찾았으므로 그 앞의 데이터를 제거
        csv_response = start_ptr;
    }
    // timetable[요일인덱스][교시인덱스]
    char *timetable[5][7] = {0}; // 포인터 배열을 NULL로 초기화

    // strtok_r 함수는 원본 문자열을 수정하므로, 복사본을 만들어 사용합니다.
    char csv_data[strlen(csv_response) + 1];
    strcpy(csv_data, csv_response);
    
    int dateCol = parseCSVHeader(csv_response, "ALL_TI_YMD"); // 시간표일자
    int timeCol = parseCSVHeader(csv_response, "PERIO"); // 교시
    int subjectCol = parseCSVHeader(csv_response, "ITRT_CNTNT"); // 수업내용
    
    char *line_saveptr;
    // 첫 번째 줄(헤더)은 건너뜁니다.
    char *current_line = strtok_r(csv_data, "\n", &line_saveptr);

    // 각 행을 반복하며 시간표를 채웁니다.
    while ((current_line = strtok_r(NULL, "\n", &line_saveptr)) != NULL) {
        char *token_saveptr;
        char *token;
        int col_index = 0;

        char *date_str = NULL;
        char *period_str = NULL;
        char *subject_str = NULL;

        // 현재 행을 ',' 기준으로 파싱하여 각 열의 데이터를 추출합니다.
        for (token = strtok_r(current_line, ",", &token_saveptr); token != NULL; token = strtok_r(NULL, ",", &token_saveptr)) {
            if (col_index == dateCol) {
                date_str = token;
            } else if (col_index == timeCol) {
                period_str = token;
            } else if (col_index == subjectCol) {
                subject_str = token;
            }
            col_index++;
        }

        // 날짜, 교시, 수업 내용이 모두 정상적으로 추출되었는지 확인합니다.
        if (date_str && period_str && subject_str) {
            remove_spaces_inplace(subject_str); // 수업 내용에서 공백 제거(공백 있으면 옆으로 너무 넓어짐)
            // 날짜 문자열로부터 요일 인덱스(월=0 ~ 금=4)를 가져옵니다.
            int day_index = get_weekday(date_str);
            // 교시 문자열을 정수로 변환합니다.
            int period_num = atoi(period_str);

            // 유효한 요일(월~금)이고 유효한 교시(1~7)인 경우에만 시간표 배열에 저장합니다.
            if (day_index != -1 && period_num >= 1 && period_num <= 7) {
                // 교시는 1부터 시작하므로 배열 인덱스는 period_num - 1 입니다.
                timetable[day_index][period_num - 1] = subject_str;
            }
        }
    }

    // 채워진 timetable 배열을 형식에 맞게 출력합니다.
    printf("%d학년 %d반 시간표(%s ~ %s)\n", grade, class, start_date, end_date);
    printf("---------------------------------------------------------------------\n");
    for (int period = 1; period <= 7; period++) {
        printf("|  %d교시 |", period);
        for (int day = 0; day < 5; day++) {
            const char* subject = timetable[day][period - 1] ? timetable[day][period - 1] : "";
            // 너비를 맞춰 출력하는 헬퍼 함수 호출
            print_padded_cell(subject);
        }
        printf("\n");
    }
    printf("---------------------------------------------------------------------\n");

                  
    return 0;
    
}

int updateSavedSchool(void) {
    FILE *file = fopen("TimeTable_school.csv", "r");
    char school_name_input[100];
    printf("학교 이름을 입력하세요: ");
    if (fgets(school_name_input, sizeof(school_name_input), stdin) == NULL) {
        fprintf(stderr, "입력 인식 실패\n");
        return -1;
    }
    school_name_input[strcspn(school_name_input, "\n")] = '\0'; // 개행 문자 제거
    
    if (searchNEISSchool(school_name_input) != -1) {
        printf("\n학교 선택 완료\n");
        printf("학교 정보:\n");
        printf("  교육청 코드: %s\n", g_selected_edu_code);
        printf("  학교 코드: %s\n", g_selected_school_code);
        // 파일에 저장
        file = fopen("TimeTable_school.csv", "w");
        if (file != NULL) {
            fprintf(file, "%s,%s,%s\n", g_selected_edu_code, g_selected_school_code, g_selected_school_name);
            fclose(file);
            printf("학교 정보가 'TimeTable_school.csv'에 저장되었습니다.\n");
            return 0;
        } else {
            fprintf(stderr, "파일을 저장할 수 없습니다.\n");
            return -1; // 파일 저장 실패
        }
    } else {
        fprintf(stderr, "학교 검색 실패\n");
        return -1; // 학교 검색 실패
    }
    
}

int main(int argc, const char * argv[]) {
    printf("##########\n");
    printf("TimeTable\n");
    printf("by Js Na\n");
    printf("##########\n");
    
    // 파일 "TimeTable_school.csv" (교육청코드, 학교코드, 학교이름) 확인
    // 없으면 검색
    // 있으면 건너뛰기
    
    // 파일 확인
    FILE *file = fopen("TimeTable_school.csv", "r");
    if (file != NULL) {
        printf("파일이 존재합니다. 학교 정보를 로드합니다.\n");
        char line[256];
        while (fgets(line, sizeof(line), file) != NULL) {
            // 파일에서 한 줄씩 읽어서 처리
            // 예: "교육청코드,학교코드,학교이름"
            char edu_code[16], school_code[32], school_name[128];
            sscanf(line, "%15[^,],%31[^,],%127[^\n]", edu_code, school_code, school_name);
            strncpy(g_selected_edu_code, edu_code, sizeof(g_selected_edu_code) - 1);
            g_selected_edu_code[sizeof(g_selected_edu_code) - 1] = '\0';
            strncpy(g_selected_school_code, school_code, sizeof(g_selected_school_code) - 1);
            g_selected_school_code[sizeof(g_selected_school_code) - 1] = '\0';
            strncpy(g_selected_school_name, school_name, sizeof(g_selected_school_name) - 1);
            
            printf("저장된 학교 정보:\n");
            printf("  교육청 코드: %s\n", g_selected_edu_code);
            printf("  학교 코드: %s\n", g_selected_school_code);
            printf("  학교 이름: %s\n", g_selected_school_name);
        }
        fclose(file);
    } else {
        printf("파일이 존재하지 않습니다. 학교 정보를 검색합니다.\n");
        updateSavedSchool(); // 학교 정보 업데이트 함수 호출
    }
    
    int grade, class;
    printf("학년을 입력해 주세요: ");
    if (scanf("%d", &grade) != 1 || grade < 1) {
        fprintf(stderr, "올바르지 않은 학년입니다.\n");
        return -1; // 유효하지 않은 학년 입력
    }
    printf("반을 입력해 주세요: ");
    if (scanf("%d", &class) != 1 || class < 1) {
        fprintf(stderr, "올바르지 않은 반입니다.\n");
        return -1; // 유효하지 않은 반 입력
    }
    // 시간표 가져오기
    if (getNEISTimeTable(grade, class) != 0) {
        fprintf(stderr, "시간표 가져오기 실패\n");
        return -1; // 시간표 가져오기 실패
    }
    return 0;
}
