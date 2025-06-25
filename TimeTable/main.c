//
//  main.c
//  TimeTable
//
//  Created by Js Na on 2025/06/17.
//  Copyright © 2025 Js Na, All rights reserved.
//

#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h>
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
    char edu_code[16]; // 교육청 코드 (ATPT_OFCDC_SC_CODE)
    char school_code[32]; // 학교 코드 (SD_SCHUL_CODE)
    char school_name[128]; // 학교 이름 (SCHUL_NM)
    char address[256]; // 학교 주소
} SchoolInfo;

#define MAX_SEARCH_RESULTS 100 // 최대 학교 검색 결과 수

// 전역 변수 선언 (프로그램 전체에서 접근 가능)
char g_selected_edu_code[16] = ""; // 선택된 교육청 코드
char g_selected_school_code[32] = ""; // 선택된 학교 코드
char g_selected_school_name[128] = ""; // 선택된 학교 이름

#define MAX_SCHOOLS 100
#define MAX_FIELD_LEN 128 // 각 필드의 최대 길이
// 문자열을 퍼센트 인코딩하여 새로운 메모리에 할당 후 반환하는 함수
char* url_encode(const char *str) {
    size_t len = strlen(str);
    size_t encoded_len = 0;

    // 1. 인코딩 후의 최종 길이를 계산
    for (size_t i = 0; i < len; i++) {
        unsigned char c = str[i];
        // 알파벳, 숫자, 그리고 일부 특수문자(-, _, ., ~)는 인코딩하지 않음
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded_len++;
        } else {
            // 그 외 모든 문자는 '%XX' 형태로 3바이트가 됨
            encoded_len += 3;
        }
    }

    // 2. 최종 길이에 맞게 메모리 할당
    char *encoded_str = (char *)malloc(encoded_len + 1);
    if (encoded_str == NULL) {
        perror("ERROR: url_encode malloc 실패");
        return NULL;
    }

    // 3. 실제 인코딩 수행
    char *p = encoded_str;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            *p++ = c;
        } else {
            // sprintf를 이용해 '%XX' 형태로 변환하여 버퍼에 씀
            p += sprintf(p, "%%%02X", c);
        }
    }
    *p = '\0'; // 문자열의 끝을 표시

    return encoded_str;
}

void trim_whitespace(char *str) {
    if (str == NULL) return;
    
    // 앞쪽 공백 제거
    char *start = str;
    while (*start && (isspace((unsigned char)*start) || (unsigned char)*start == 0xEF || (unsigned char)*start == 0xBB || (unsigned char)*start == 0xBF)) {
        // isspace()로 공백 제거
        start += 1;
    }
    
    // 뒤쪽 공백 제거
    char *end = str + strlen(str) - 1;
    while (end >= start && (isspace((unsigned char)*end) || (unsigned char)*end == 0xEF || (unsigned char)*end == 0xBB || (unsigned char)*end == 0xBF)) {
        end--;
    }
    *(end + 1) = '\0';
    
    // 문자열이 변경되었으면 시작 위치로 이동
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

// Gemini Helped me fix this.
char* getURL(char *hostname, char *path, int port) { // FIXME: Too short query will cause pharsing error since the API will return EXTREMELY long response.
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char request_buffer[RECV_BUFFER_SIZE]; // 요청 버퍼는 충분히 크게 설정
    
    // 전체 응답을 저장할 동적 버퍼와 관련 변수
    char *full_response = NULL;
    size_t full_response_size = 0;

    const char *user_agent_string = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";

    // 1. 소켓 생성
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR: 소켓 생성 실패");
        return NULL;
    }

    // 2. DNS Lookup
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr, "ERROR: %s 호스트 찾기 실패\n", hostname);
        close(sockfd);
        return NULL;
    }

    // 3. 요청 보낼 서버 주소 설정 (bzero -> memset, bcopy -> memcpy)
    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy((char *)&serv_addr.sin_addr.s_addr,
           (char *)server->h_addr_list[0],
           server->h_length);
    serv_addr.sin_port = htons(port);

    // 4. 연결
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR: 연결 실패");
        close(sockfd);
        return NULL;
    }

    // 5. HTTP 요청 메시지 생성 및 전송
    snprintf(request_buffer, sizeof(request_buffer),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, hostname, user_agent_string);

    printf("요청 전송\n%s", request_buffer);

    if (send(sockfd, request_buffer, strlen(request_buffer), 0) < 0) {
        perror("ERROR: 소켓 쓰기 실패");
        close(sockfd);
        return NULL;
    }

    // 6. 응답 수신 (수정된 로직)
    char temp_buffer[RECV_BUFFER_SIZE];
    ssize_t bytes_received; // 반환 타입에 맞는 변수 타입 사용 (int -> ssize_t)

    // 서버가 연결을 닫을 때까지 모든 데이터를 수신하여 full_response에 추가
    while ((bytes_received = recv(sockfd, temp_buffer, RECV_BUFFER_SIZE - 1, 0)) > 0) {
        char *new_response = realloc(full_response, full_response_size + bytes_received);
        if (new_response == NULL) {
            perror("ERROR: realloc 실패");
            free(full_response);
            close(sockfd);
            return NULL;
        }
        full_response = new_response;
        memcpy(full_response + full_response_size, temp_buffer, bytes_received);
        full_response_size += bytes_received;
    }
    
    if (bytes_received < 0) {
        perror("ERROR: 소켓 읽기 실패");
        free(full_response);
        close(sockfd);
        return NULL;
    }

    // 수신이 완료된 후, 전체 응답에 널 종료 문자 추가
    if (full_response) {
        char *new_response = realloc(full_response, full_response_size + 1);
        if (new_response == NULL) {
             perror("ERROR: realloc 실패");
             free(full_response);
             close(sockfd);
             return NULL;
        }
        full_response = new_response;
        full_response[full_response_size] = '\0';
    } else {
        // 아무 데이터도 받지 못한 경우
        printf("응답 수신 실패 (0 바이트).\n");
        close(sockfd);
        return NULL;
    }

    // 7. 소켓 닫기
        close(sockfd);
        /*printf("전체 응답 수신 완료. 총 %zu 바이트 수신\n", full_response_size);

        // ========================= 진단 코드 추가 =========================
        printf("\n--- 수신된 전체 응답 (헤더 포함) ---\n");
        if (full_response) {
            printf("%s", full_response);
        }
        printf("\n--- 전체 응답 끝 ---\n\n");
        // =================================================================
         */
        // 8. 헤더와 본문 분리
        char *header_end = strstr(full_response, "\r\n\r\n");
        char *response_body = NULL;
    if (header_end) {
        // 헤더 끝 다음부터가 본문의 시작
        char *body_start = header_end + 4; // "\r\n\r\n" 의 길이 4
        size_t body_length = full_response_size - (body_start - full_response);
        
        response_body = (char *)malloc(body_length + 1);
        if (response_body == NULL) {
            perror("ERROR: 본문 메모리 할당 실패");
            free(full_response);
            return NULL;
        }
        memcpy(response_body, body_start, body_length);
        response_body[body_length] = '\0';
    } else {
        printf("HTTP 응답에서 헤더 구분자를 찾을 수 없음\n");
        // 본문이 없는 응답일 수 있으므로 full_response 자체를 반환하거나 NULL을 반환할 수 있음
        // 여기서는 에러로 간주하지 않고 빈 문자열을 할당
        response_body = (char *)malloc(1);
        response_body[0] = '\0';
    }

    //printf("응답 본문:\n%s\n", response_body);
    // 전체 응답을 저장했던 임시 버퍼는 해제
    free(full_response);

    return response_body;
}
int searchNEISSchool(char *schoolName) {
    char full_path[256];
    char *encodedSchoolName = url_encode(schoolName);
    
    sprintf(full_path, "/https://hello.jsna.dev/lunch/api/search-hakgyo.php?sc=%s", encodedSchoolName);
    
    char* json_response = getURL("cors-proxy.jsna.workers.dev", full_path, 80);
    
    if (json_response == NULL) {
        fprintf(stderr, "잘못된 응답\n");
        return -1; // 검색 실패
    }
    
    //printf("\nJSON 응답:\n%s\n", json_response);
    
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
    
    // 'schoolInfo' 배열에서 'row' 배열 찾기
    JSON_Array *row_array = NULL;
    for (size_t i = 0; i < json_array_get_count(school_info_array); i += 1) {
        JSON_Object *current_obj_in_school_info = json_array_get_object(school_info_array, i);
        if (current_obj_in_school_info != NULL) {
            row_array = json_object_get_array(current_obj_in_school_info, "row");
            if (row_array != NULL) {
                break; // 'row' 배열을 찾아서 루프 종료
            }
        }
    }
    
    if (row_array == NULL) {
        fprintf(stderr, "ERROR: 'schoolInfo'에서 'row' 찾기 실패\n");
        json_value_free(root_value);
        free(json_response);
        return -1;
    }
    
    // "row" 배열 반복해서 각 학교 정보 추출
    size_t i;
    for (i = 0; i < json_array_get_count(row_array) && num_schools < MAX_SEARCH_RESULTS; i += 1) {
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
            num_schools += 1;
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
    for (int j = 0; j < num_schools; j += 1) {
        printf("%d. %s (교육청: %s, 코드: %s)\n", j + 1,
               schools[j].school_name, schools[j].edu_code, schools[j].school_code);
        printf("   주소: %s\n", schools[j].address); // 주소 출력
        
    }
    
    int choice;
    printf("학교를 선택해 주세요. (1-%d 번호 입력): ", num_schools);
    if (scanf("%d", &choice) != 1 || choice < 1 || choice > num_schools) {
        printf("올바르지 않은 선택입니다.\n");
        // 잘못된 입력
        // TODO: 재시도 기능 추가
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
    
    // 복사본을 만들어 사용
    char *csvCopy = strdup(csvString);
    if (csvCopy == NULL) {
        printf("메모리 할당에 실패했습니다.\n");
        return -1;
    }
    
    // 헤더만 추출
    char *header = strtok(csvCopy, "\n\r"); // \r (캐리지 리턴)도 처리
    
    if (header != NULL) {
        // BOM(Byte Order Mark) 제거
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
            if (strcmp(token, searchHeader) == 0) {
                free(csvCopy); // 함수 종료 전 메모리 해제
                return index;
            }
            token = strtok(NULL, ",");
            index += 1;
        }
        
        printf("'%s' 헤더를 찾을 수 없습니다.\n", searchHeader);
        free(csvCopy);
        return -1;
    } else {
        printf("CSV 문자열에서 헤더를 추출할 수 없습니다.\n");
        free(csvCopy);
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

// 문자열의 실제 화면 너비를 계산하는 함수
int get_str_width(const char *s) {
    wchar_t wstr[1024];
    // 멀티바이트 문자열(UTF-8)을 와이드 캐릭터 문자열로 변환
    int len = mbstowcs(wstr, s, 1024); // FIXME: Implicit conversion loses integer precision: 'size_t' (aka 'unsigned long') to 'int'
    if (len < 0) return strlen(s); // 변환 실패 시 바이트 길이 반환
    // 와이드 캐릭터 문자열의 화면 너비 계산
    return wcswidth(wstr, len);
}

// 문자열을 출력하고 너비에 맞게 공백을 추가하는 함수
void print_padded_cell(const char *subject, int target_width, int is_today) {
    int width = get_str_width(subject);
    // 목표 너비에서 실제 너비를 뺀 만큼 공백 추가
    int padding = target_width - width;
    if (padding < 0) padding = 0; // 너비를 초과하면 공백 없음

    if (is_today == 1) {
        printf("#");
    } else {
        printf(" ");
    }
        
    printf("%s", subject);
    for (int i = 0; i < padding; i += 1) {
        printf(" ");
    }
    if (is_today == 1) {
        printf("#");
    }
    printf("|");
}

void remove_spaces_inplace(char *str) {
    if (str == NULL) return; // 널 포인터가 들어온 경우 함수 종료

    char *writer = str; // 공백이 아닌 문자를 써야 할 위치를 가리키는 포인터
    char *reader = str; // 문자열을 처음부터 읽어나갈 포인터

    // 문자열의 끝에 도달할 때까지 반복
    while (*reader != '\0') {
        // 현재 reader 문자가 공백이 아니라면
        if (*reader != ' ') {
            *writer = *reader; // writer 위치에 해당 문자를 복사
            writer += 1;          // writer 위치를 다음 칸으로 이동
        }
        // reader 항상 다음 문자로 이동
        reader += 1;
    }
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
    
    // 일주일 후의 날짜
    // 여기서는 단순히 오늘부터 6일 후로 계산
    
    tm_info->tm_mday += 6;
    mktime(tm_info); // mktime으로 tm_info를 정규화하여 월, 일 등을 업데이트
    strftime(end_date, sizeof(end_date), "%Y%m%d", tm_info);
    
    
    // 날짜 매개변수 추가해서 요청 보내기 (fy: from_ymd, ty: to_ymd)
    // 서버에서 NEIS API 응답(JSON) -> CSV 변환
    sprintf(full_path, "/https://hello.jsna.dev/timetable/get-timetable.php?oc=%s&sc=%s&gd=%d&cl=%d&fy=%s&ty=%s",
            g_selected_edu_code, g_selected_school_code, grade, class, start_date, end_date);
    
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

    // 복사본을 만들어 사용
    char csv_data[strlen(csv_response) + 1];
    strcpy(csv_data, csv_response);
    
    int dateCol = parseCSVHeader(csv_response, "ALL_TI_YMD"); // 시간표일자
    int timeCol = parseCSVHeader(csv_response, "PERIO"); // 교시
    int subjectCol = parseCSVHeader(csv_response, "ITRT_CNTNT"); // 수업내용
    
    char *line_saveptr;
    // 헤더 건너뛰기
    char *current_line = strtok_r(csv_data, "\n", &line_saveptr);

    // 각 행 반복해서 시간표 채우기
    while ((current_line = strtok_r(NULL, "\n", &line_saveptr)) != NULL) {
        char *token_saveptr;
        char *token;
        int col_index = 0;

        char *date_str = NULL;
        char *period_str = NULL;
        char *subject_str = NULL;

        // 현재 행 ',' 기준으로 파싱해서 각 열의 데이터 추출
        for (token = strtok_r(current_line, ",", &token_saveptr); token != NULL; token = strtok_r(NULL, ",", &token_saveptr)) {
            if (col_index == dateCol) {
                date_str = token;
            } else if (col_index == timeCol) {
                period_str = token;
            } else if (col_index == subjectCol) {
                subject_str = token;
            }
            col_index += 1;
        }

        // 날짜, 교시, 수업 내용이 모두 정상적으로 추출되었는지 확인
        if (date_str && period_str && subject_str) {
            remove_spaces_inplace(subject_str); // 수업 내용에서 공백 제거(공백 있으면 옆으로 너무 넓어짐)
            int day_index = get_weekday(date_str); // 날짜에서 요일 인덱스 가져오기
            int period_num = atoi(period_str);
            if (day_index != -1 && period_num >= 1 && period_num <= 7) { // 유효한 요일과 교시인지 확인
                timetable[day_index][period_num - 1] = subject_str;
            }
        }
    }

    // 채워진 timetable 배열을 형식에 맞게 출력
    printf("%d학년 %d반 시간표(%s ~ %s)\n", grade, class, start_date, end_date);
    printf("---------------------------------------------------------------------\n");
    int max_width = 0;
    
    for (int day = 0; day < 5; day += 1) {
        for (int period = 0; period < 7; period += 1) {
            const char* subject = timetable[day][period] ? timetable[day][period] : "";
            int width = get_str_width(subject);
            if (width > max_width) {
                max_width = width; // 가장 긴 수업 내용의 너비를 저장
            }
        }
    }
    int heading_indent = get_str_width("|0교시|"); // "교시"의 너비 계산
    for (int i = 0; i < heading_indent; i += 1) {
        printf(" "); // 교시 제목 앞에 공백 추가
    }
    const char* date[5] = {"월요일", "화요일", "수요일", "목요일", "금요일"};
    int today = get_weekday(start_date); // 오늘 날짜의 요일 인덱스
    for (int day = 0; day < 5; day += 1) { // 요일 제목 출력
        if (day == today) {
            char print_date[50] = "";
            strcpy(print_date, date[day]);
            strcat(print_date, "(오늘)"); // 오늘 날짜 표시
            print_padded_cell(print_date, max_width, 1); // 오늘이면 # 추가
        } else {
            print_padded_cell(date[day], max_width, 0); // 오늘이 아니면 그냥 출력
        }
    }
    
    printf("\n---------------------------------------------------------------------\n");
    for (int period = 1; period <= 7; period += 1) {
        printf("|%d교시|", period);
        for (int day = 0; day < 5; day += 1) {
            const char* subject = timetable[day][period - 1] ? timetable[day][period - 1] : "";
            if (day == today) {
                // 오늘이면 과목 앞에 # 추가
                print_padded_cell(subject, max_width, 1);
            } else {
                print_padded_cell(subject, max_width, 0);
            }
        }
        printf("\n");
    }
    printf("---------------------------------------------------------------------\n");

                  
    return 0;
    
}

int updateSavedSchool(void) {
    FILE *file = fopen("TimeTable_school.csv", "r");
    char school_name_input[100] = "";
    printf("학교 이름을 입력하세요: ");
    scanf(" %[^\n]", school_name_input); // 공백 포함 입력 받기
    // 개행 문자 제거
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
    printf("Enter 키를 눌러 종료합니다.\n");
    printf("학교를 변경하려면 1, 다시 시작하려면 2를 입력하세요.\n");
    int choice;
    if (scanf("%d", &choice) != 1 || (choice != 1 && choice != 2)) {
        fprintf(stderr, "올바르지 않은 선택입니다.\n");
        return -1; // 유효하지 않은 선택
    }
    if (choice == 1) {
        // 학교 정보 업데이트
        if (updateSavedSchool() != 0) {
            fprintf(stderr, "학교 정보 업데이트 실패\n");
            return -1; // 학교 정보 업데이트 실패
        }
        return main(argc, argv); // 재귀
    } else if (choice == 2) {
        // 프로그램 다시 시작
        printf("프로그램을 다시 시작합니다.\n");
        return main(argc, argv); // 재귀
    }
    return 0;
}
