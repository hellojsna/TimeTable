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

#include "parson.h" // JSON 파싱 라이브러리


#define RECV_BUFFER_SIZE 4096 // 서버 응답을 받을 버퍼 크기
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

int extract_value_for_key(char **json_ptr, const char *key_name, char *output_buffer, size_t buffer_size) {
    char search_key[MAX_FIELD_LEN + 4]; // "key_name": 형태를 찾기 위함
    sprintf(search_key, "\"%s\":", key_name);
    
    char *key_pos = strstr(*json_ptr, search_key);
    if (!key_pos) {
        // fprintf(stderr, "Key '%s' not found.\n", key_name); // 디버깅용
        return 0;
    }
    
    // 키를 찾았으면, 값의 시작인 다음 따옴표를 찾습니다.
    char *value_start = strchr(key_pos + strlen(search_key), '"');
    if (!value_start) return 0;
    value_start++; // 따옴표 다음 문자부터 시작
    
    char *value_end = strchr(value_start, '"');
    if (!value_end) return 0;
    
    size_t len = value_end - value_start;
    if (len >= buffer_size) {
        len = buffer_size - 1; // 버퍼 오버플로우 방지
    }
    strncpy(output_buffer, value_start, len);
    output_buffer[len] = '\0'; // 널 종료
    
    *json_ptr = value_end + 1; // json_ptr 업데이트
    return 1;
}

/**
 * @brief 주어진 JSON 문자열에서 학교 정보 목록을 파싱합니다.
 * "schoolInfo" -> "row" 경로를 따라 데이터를 찾습니다.
 * @param json_string 파싱할 JSON 문자열.
 * @param schools 파싱된 학교 정보를 저장할 SchoolInfo 배열.
 * @param max_schools schools 배열의 최대 크기.
 * @return 파싱된 학교 정보의 수, 오류 발생 시 -1.
 */
int parse_complex_school_json(const char *json_string, SchoolInfo *schools, int max_schools) {
    char *json_mutable = strdup(json_string); // 원본 문자열을 수정하지 않기 위해 복사
    if (!json_mutable) {
        perror("Memory allocation failed for json_mutable");
        return -1;
    }
    char *current_pos = json_mutable;
    int school_count = 0;
    
    // 1. "schoolInfo" 키 찾기
    current_pos = strstr(current_pos, "\"schoolInfo\":");
    if (!current_pos) {
        fprintf(stderr, "ERROR: 'schoolInfo' key not found.\n");
        free(json_mutable);
        return -1;
    }
    current_pos += strlen("\"schoolInfo\":"); // "schoolInfo": 다음으로 이동
    
    // 2. "schoolInfo" 배열의 시작 '[' 찾기
    current_pos = strchr(current_pos, '[');
    if (!current_pos) {
        fprintf(stderr, "ERROR: 'schoolInfo' array start '[' not found.\n");
        free(json_mutable);
        return -1;
    }
    current_pos++; // '[' 다음으로 이동
    
    // 3. "row" 키 찾기 (이 예시에서는 schoolInfo 배열의 두 번째 요소에 있다고 가정)
    //    이 부분을 더 견고하게 하려면, schoolInfo 배열을 순회하며 'row' 키를 가진 객체를 찾아야 함.
    current_pos = strstr(current_pos, "\"row\":");
    if (!current_pos) {
        fprintf(stderr, "ERROR: 'row' key not found.\n");
        free(json_mutable);
        return -1;
    }
    current_pos += strlen("\"row\":"); // "row": 다음으로 이동
    
    // 4. "row" 배열의 시작 '[' 찾기
    current_pos = strchr(current_pos, '[');
    if (!current_pos) {
        fprintf(stderr, "ERROR: 'row' array start '[' not found.\n");
        free(json_mutable);
        return -1;
    }
    current_pos++; // '[' 다음으로 이동
    
    // 5. 각 학교 정보 객체 순회
    //    '{'를 찾아 각 객체의 시작을 식별
    while (school_count < max_schools && (current_pos = strchr(current_pos, '{'))) {
        current_pos++; // '{' 다음으로 이동 (객체 시작)
        
        // 각 필드 추출
        if (!extract_value_for_key(&current_pos, "ATPT_OFCDC_SC_CODE", schools[school_count].edu_code, sizeof(schools[school_count].edu_code))) {
            fprintf(stderr, "ERROR: Failed to extract ATPT_OFCDC_SC_CODE for school %d\n", school_count);
            // break; // 오류 발생 시 바로 중단
        }
        if (!extract_value_for_key(&current_pos, "SD_SCHUL_CODE", schools[school_count].school_code, sizeof(schools[school_count].school_code))) {
            fprintf(stderr, "ERROR: Failed to extract SD_SCHUL_CODE for school %d\n", school_count);
            // break;
        }
        if (!extract_value_for_key(&current_pos, "SCHUL_NM", schools[school_count].school_name, sizeof(schools[school_count].school_name))) {
            fprintf(stderr, "ERROR: Failed to extract SCHUL_NM for school %d\n", school_count);
            // break;
        }
        
        school_count++;
        
        // 현재 객체의 끝 '}'을 찾기
        current_pos = strchr(current_pos, '}');
        if (!current_pos) {
            fprintf(stderr, "ERROR: Invalid JSON format - missing '}' for object.\n");
            break;
        }
        current_pos++; // '}' 다음으로 이동
        
        // 다음 객체 또는 배열 끝을 식별
        char *next_comma = strchr(current_pos, ',');
        char *next_bracket = strchr(current_pos, ']'); // 'row' 배열의 끝 ']'
        
        if (next_comma && (!next_bracket || next_comma < next_bracket)) {
            // 다음 학교 데이터가 있음
            current_pos = next_comma + 1;
        } else if (next_bracket) {
            // 'row' 배열의 끝에 도달
            break;
        } else {
            // 예상치 못한 종료 또는 잘못된 형식
            fprintf(stderr, "ERROR: Invalid JSON format - unexpected end or character after object.\n");
            break;
        }
    }
    
    free(json_mutable);
    return school_count;
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

char* searchNEISSchool(char *schoolName) {
    char full_path[256];
    // strcat은 첫 번째 인자(대상)에 두 번째 인자(원본)를 이어붙이고 첫 번째 인자를 반환합니다.
    // 따라서 안전하게 사용하려면 충분한 버퍼를 먼저 만들고 sprintf 등을 사용해야 합니다.
    // 여기서는 동적 프록시 경로와 쿼리 파라미터를 조합합니다.
    sprintf(full_path, "/https://hello.jsna.dev/lunch/api/search-hakgyo.php?sc=%s", schoolName);
    
    char* json_response = getURL("cors-proxy.jsna.workers.dev", full_path, 80);
    
    if (json_response == NULL) {
        fprintf(stderr, "Failed to get school search JSON response.\n");
        return NULL; // 검색 실패
    }
    
    printf("\nReceived JSON response:\n%s\n", json_response);
    
    SchoolInfo schools[MAX_SEARCH_RESULTS];
    int num_schools = 0;
    
    // JSON 파싱 시작
    JSON_Value *root_value = json_parse_string(json_response);
    if (root_value == NULL) {
        fprintf(stderr, "ERROR: Failed to parse JSON string.\n");
        free(json_response);
        return NULL;
    }
    
    JSON_Object *root_object = json_value_get_object(root_value);
    if (root_object == NULL) {
        fprintf(stderr, "ERROR: Root is not an object.\n");
        json_value_free(root_value);
        free(json_response);
        return NULL;
    }
    
    JSON_Array *school_info_array = json_object_get_array(root_object, "schoolInfo");
    if (school_info_array == NULL) {
        fprintf(stderr, "ERROR: 'schoolInfo' array not found.\n");
        json_value_free(root_value);
        free(json_response);
        return NULL;
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
        fprintf(stderr, "ERROR: 'row' array not found within 'schoolInfo'.\n");
        json_value_free(root_value);
        free(json_response);
        return NULL;
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
        printf("No schools found for '%s'.\n", schoolName);
        return NULL;
    }
    
    // 사용자에게 학교 선택 요청
    printf("\n--- Search Results for '%s' ---\n", schoolName);
    for (int j = 0; j < num_schools; ++j) {
        printf("%d. %s (교육청: %s, 코드: %s)\n", j + 1,
               schools[j].school_name, schools[j].edu_code, schools[j].school_code);
        printf("   주소: %s\n", schools[j].address); // 주소 출력

    }
    
    int choice;
    printf("Please enter the number of the school you want to select (1-%d): ", num_schools);
    // 사용자 입력이 숫자가 아니거나 범위를 벗어나는 경우에 대한 강력한 유효성 검사 필요
    if (scanf("%d", &choice) != 1 || choice < 1 || choice > num_schools) {
        printf("Invalid selection. Please try again.\n");
        // 유효하지 않은 선택이므로 전역 변수에 저장하지 않고 실패 반환
        // TODO: 재시도 로직 추가 가능
        return NULL;
    }
    
    // 선택된 학교 정보 전역 변수에 저장
    strncpy(g_selected_edu_code, schools[choice - 1].edu_code, sizeof(g_selected_edu_code) - 1);
    g_selected_edu_code[sizeof(g_selected_edu_code) - 1] = '\0';
    
    strncpy(g_selected_school_code, schools[choice - 1].school_code, sizeof(g_selected_school_code) - 1);
    g_selected_school_code[sizeof(g_selected_school_code) - 1] = '\0';
    
    strncpy(g_selected_school_name, schools[choice - 1].school_name, sizeof(g_selected_school_name) - 1);
    // 버퍼 비우기 (scanf 후 남은 개행 문자 처리)
    while (getchar() != '\n');
    
    printf("\nSelected School:\n");
    printf("  교육청 코드: %s\n", g_selected_edu_code);
    printf("  학교 코드: %s\n", g_selected_school_code);
    printf("  학교 이름: %s\n", g_selected_school_name);
    
    return (char*)1; // 성공을 나타내는 임의의 non-NULL 값 반환. 실제 데이터는 전역 변수에 저장됨.
    // (혹은 int 반환형으로 변경하여 0/1을 반환하는 것이 더 자연스러움)
}
char* getNEISTimeTable(void) { // NEIS OpenAPI를 이용해 시간표를 가져오기
    
    return "";
}

int updateSavedSchool(void) {
    FILE *file = fopen("TimeTable_school.csv", "r");
    char school_name_input[100];
    printf("학교 이름을 입력하세요: ");
    if (fgets(school_name_input, sizeof(school_name_input), stdin) == NULL) {
        fprintf(stderr, "입력 인식 실패\n");
        return 1;
    }
    school_name_input[strcspn(school_name_input, "\n")] = '\0'; // 개행 문자 제거
    
    if (searchNEISSchool(school_name_input) != NULL) {
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
            return 1; // 파일 저장 실패
        }
    } else {
        fprintf(stderr, "학교 검색 실패\n");
        return 1; // 학교 검색 실패
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
    
    return 0;
}
