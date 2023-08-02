#pragma warning(disable : 4996)

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

using std::string;

#ifdef _MSC_VER
using MFT_ENUM_DATA_T = MFT_ENUM_DATA_V0;
#else
using MFT_ENUM_DATA_T = MFT_ENUM_DATA;
#endif

const int BUF_LEN = 1E6;
const int PATH_BUFFER_LEN = 4096;

struct USN_RECORT_T {
  using ID_T = DWORDLONG;
  ID_T parentID;
  ID_T currentID;
  std::string filename;
};

inline bool operator<(const USN_RECORT_T& l, const USN_RECORT_T& r) {
  return l.currentID < r.currentID;
}

auto solve(char fileName[]) {
  std::printf("Volume: %s\n", fileName);
  // 调用该函数需要管理员权限
  HANDLE hVol = CreateFileA(
    fileName,
    GENERIC_READ | GENERIC_WRITE,     // 可以为0
    FILE_SHARE_READ | FILE_SHARE_WRITE, // 必须包含有FILE_SHARE_WRITE
    NULL,                 // 这里不需要
    OPEN_EXISTING, // 必须包含OPEN_EXISTING, CREATE_ALWAYS可能会导致错误
    FILE_ATTRIBUTE_READONLY, // FILE_ATTRIBUTE_NORMAL可能会导致错误
    NULL);           // 这里不需要
  if (INVALID_HANDLE_VALUE != hVol) {
    std::printf("Get volume handle sucess\n ");
  }
  else {
    std::printf("Get volume handle failed -- handle:%x error:%d\n", hVol,
      GetLastError());
    std::exit(1);
  }
  std::printf("status: 0 error:%d\n", GetLastError());

  DWORD br;
  CREATE_USN_JOURNAL_DATA cujd;
  cujd.MaximumSize = 0;   // 0表示使用默认值
  cujd.AllocationDelta = 0; // 0表示使用默认值
  BOOL status = DeviceIoControl(hVol, FSCTL_CREATE_USN_JOURNAL, &cujd,
    sizeof(cujd), NULL, 0, &br, NULL);

  std::printf("status:%x error:%d\n", status, GetLastError());
  if (0 != status) {
    std::printf("Init USN success\n");
  }
  else {
    std::printf("Init USN failed -- status:%x error:%d\n", status,
      GetLastError());
    std::exit(1);
  }

  bool getBasicInfoSuccess = false;
  USN_JOURNAL_DATA UsnInfo;
  status = DeviceIoControl(hVol, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &UsnInfo,
    sizeof(UsnInfo), &br, NULL);
  if (0 != status) {
    std::printf("Get USN Success\n");
  }
  else {
    std::printf("Get USN Failed -- status:%x error:%d\n", status,
      GetLastError());
    std::exit(1);
  }

  MFT_ENUM_DATA_T med = {
    0,         // StartFileReferenceNumber
    0,         // UsnInfo.FirstUsn, LowUsn
    UsnInfo.NextUsn, // NextUsn
  };
  CHAR buffer[BUF_LEN]; // 用于储存记录的缓冲 , 尽量足够地大
  DWORD usnDataSize;
  PUSN_RECORD UsnRecord;

  std::vector<USN_RECORT_T> vec;

  while (true) {
    bool RET = DeviceIoControl(hVol, FSCTL_ENUM_USN_DATA, &med, sizeof(med),
      buffer, BUF_LEN, &usnDataSize, NULL);
    std::printf("Error Code: %d\n", GetLastError());
    if (RET == 0)
      break;
    std::printf("Numbers of returned: %d\n", usnDataSize);
    DWORD dwRetBytes = usnDataSize - sizeof(USN);
    UsnRecord = (PUSN_RECORD)(((PCHAR)buffer) + sizeof(USN));
    while (dwRetBytes > 0) {
      // 打印获取到的信息
      const int strLen = UsnRecord->FileNameLength;
      char fileName[PATH_BUFFER_LEN] = { 0 };
      WideCharToMultiByte(CP_OEMCP, WC_COMPOSITECHECK, UsnRecord->FileName,
        strLen / 2, fileName, strLen, NULL, FALSE);
      vec.push_back({ UsnRecord->ParentFileReferenceNumber,
               UsnRecord->FileReferenceNumber, fileName });
      // if (check(fileName)) {
      // std::printf("FileName: %s\n", fileName);
      // // // 下面两个 filereferencenumber 可以用来获取文件的路径信息
      // std::printf("FileReferenceNumber: %xI64\n",
      //       UsnRecord->FileReferenceNumber);
      // std::printf("ParentFileReferenceNumber: %xI64\n",
      //       UsnRecord->ParentFileReferenceNumber);
      // std::printf("\n");
      // }
      // char* pathBuffer[BUF_LEN];
      // 获取下一个记录
      DWORD recordLen = UsnRecord->RecordLength;
      dwRetBytes -= recordLen;
      UsnRecord = (PUSN_RECORD)(((PCHAR)UsnRecord) + recordLen);
    }
    med.StartFileReferenceNumber = *(USN*)&buffer;
  }
  std::sort(vec.begin(), vec.end());
  return vec;
}

inline bool check(const string& s, const char pattern[]) {
  int len = std::strlen(pattern);
  // 因为 std::string 末尾会有 '\0'，所以不会越界
  for (int i = 0; i < len; i++) {
    if (s[i] != pattern[i])
      return false;
  }
  return true;
}

inline bool check2(const string& s, const char pattern[]) {
  return s.find(pattern) != std::string::npos;
}

int main() {
  char volume[PATH_BUFFER_LEN];
  std::printf("Input volume(etc `\\\\.\\C:`): \n");
  std::scanf("%s", volume);
  std::printf("INDEXING\n");
  auto vec = solve(volume);
  std::printf("INDEX FINISHED!!!\n");
  char pattern[PATH_BUFFER_LEN];
  auto getPath = [&vec](auto& self, USN_RECORT_T::ID_T id) -> std::string {
    auto iter =
      std::lower_bound(vec.begin(), vec.end(), USN_RECORT_T{ 0, id, "" });
    if (iter != vec.end() && iter->currentID == id) {
      const auto& [parentID, currentID, filename] = *iter;
      std::string parent_ret = self(self, parentID);
      parent_ret.push_back('/');
      parent_ret.append(filename.begin(), filename.end());
      return parent_ret;
    }
    else {
      return "";
    }
  };
  while (true) {
    std::printf("Please input pattern: ");
    std::scanf("%s", pattern);
    for (const auto& [parentID, currentID, filename] : vec) {
      if (check2(filename, pattern)) {
        const auto path = getPath(getPath, currentID);
        std::printf("File Name: %s\n", filename.c_str());
        std::printf("Path: %s\n", path.c_str());
        std::printf("\n");
      }
    }
  }
}
