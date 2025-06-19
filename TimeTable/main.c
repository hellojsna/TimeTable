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

#define RECV_BUFFER_SIZE 4096 // 서버 응답을 받을 버퍼 크기

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
    printf(getURL("cors-proxy.jsna.workers.dev", "/https://hello.jsna.dev/timetable/get-timetable.php?oc=J10&sc=7530854&fy=20250615&ty=20250621", 80)); // NEIS OpenAPI Wrapper
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
