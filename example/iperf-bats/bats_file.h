/**
 * @file bats_file.h
 * @author Peng LEI (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-09-08
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#ifndef SRC_EXAMPLE_IPERF_BATS_BATS_FILE_H_
#define SRC_EXAMPLE_IPERF_BATS_BATS_FILE_H_

#include <string>
#include <unordered_map>
#include <utility>

// Enum for file extensions (used to index into the extension table)
enum class FileExtension : uint16_t {
  TXT = 0,
  MD,
  LOG,
  JSON,
  YAML,
  YML,
  XML,
  INI,
  CONF,
  ENV,
  SH,
  PY,
  JS,
  TS,
  C,
  H,
  CPP,
  HPP,
  JAVA,
  GO,
  RS,
  O,
  SO,
  DLL,
  DYLIB,
  A,
  LIB,
  EXE,
  BIN,
  PT,
  PTH,
  SAFETENSORS,
  ONNX,
  TFLITE,
  PB,
  H5,
  GGML,
  GGUF,
  TAR,
  TAR_GZ,
  ZIP,
  SEVENZ,
  GZ,
  BZ2,
  XZ,
  ISO,
  HTML,
  CSS,
  JPG,
  PNG,
  GIF,
  SVG,
  WEBP,
  MP3,
  WAV,
  FLAC,
  MP4,
  MKV,
  PDF,
  DOCX,
  XLSX,
  PPTX,
  PEM,
  CRT,
  KEY,
  MD5,
  SHA1,
  SHA256,
  UNKNOWN,
  COUNT  // keep as last
};

// use enum-keyed unordered_map: provide a simple hash for the enum
struct FileExtensionHash {
  std::size_t operator()(FileExtension fe) const noexcept { return std::hash<uint16_t>{}(static_cast<uint16_t>(fe)); }
};

// Hash table keyed by FileExtension: value is (ext string, description)
static const std::unordered_map<FileExtension, std::pair<std::string, std::string>, FileExtensionHash> FileExtTable = {
    {FileExtension::TXT, {".txt", "plain text file"}},
    {FileExtension::MD, {".md", "Markdown formatted text (documentation, README)"}},
    {FileExtension::LOG, {".log", "log output file"}},
    {FileExtension::JSON, {".json", "JSON data/configuration"}},
    {FileExtension::YAML, {".yaml", "YAML data/configuration"}},
    {FileExtension::YML, {".yml", "YAML data/configuration"}},
    {FileExtension::XML, {".xml", "XML data/configuration"}},
    {FileExtension::INI, {".ini", "simple key=value config file"}},
    {FileExtension::CONF, {".conf", "configuration file"}},
    {FileExtension::ENV, {".env", "environment variable definitions"}},
    {FileExtension::SH, {".sh", "shell script"}},
    {FileExtension::PY, {".py", "Python source file"}},
    {FileExtension::JS, {".js", "JavaScript source file"}},
    {FileExtension::TS, {".ts", "TypeScript source file"}},
    {FileExtension::C, {".c", "C source file"}},
    {FileExtension::H, {".h", "C header file"}},
    {FileExtension::CPP, {".cpp", "C++ source file"}},
    {FileExtension::HPP, {".hpp", "C++ header file"}},
    {FileExtension::JAVA, {".java", "Java source file"}},
    {FileExtension::GO, {".go", "Go source file"}},
    {FileExtension::RS, {".rs", "Rust source file"}},
    {FileExtension::O, {".o", "object file"}},
    {FileExtension::SO, {".so", "shared library (Linux)"}},
    {FileExtension::DLL, {".dll", "shared library (Windows)"}},
    {FileExtension::DYLIB, {".dylib", "shared library (macOS)"}},
    {FileExtension::A, {".a", "static library archive"}},
    {FileExtension::LIB, {".lib", "static library (Windows)"}},
    {FileExtension::EXE, {".exe", "executable binary (Windows)"}},
    {FileExtension::BIN, {".bin", "generic binary file / model weights (HuggingFace pytorch_model.bin)"}},
    {FileExtension::PT, {".pt", "PyTorch model checkpoint"}},
    {FileExtension::PTH, {".pth", "PyTorch model checkpoint"}},
    {FileExtension::SAFETENSORS, {".safetensors", "Safe tensor model format (preferred for sharing)"}},
    {FileExtension::ONNX, {".onnx", "ONNX model for interoperable runtimes"}},
    {FileExtension::TFLITE, {".tflite", "TensorFlow Lite model"}},
    {FileExtension::PB, {".pb", "TensorFlow protobuf model / checkpoint"}},
    {FileExtension::H5, {".h5", "Keras HDF5 model"}},
    {FileExtension::GGML, {".ggml", "GGML model format (llama.cpp)"}},
    {FileExtension::GGUF, {".gguf", "GGUF model format (llama.cpp ecosystem)"}},
    {FileExtension::TAR, {".tar", "tar archive"}},
    {FileExtension::TAR_GZ, {".tar.gz", ".tar.gz compressed archive"}},
    {FileExtension::ZIP, {".zip", "zip archive"}},
    {FileExtension::SEVENZ, {".7z", "7-zip archive"}},
    {FileExtension::GZ, {".gz", "gzip compressed file"}},
    {FileExtension::BZ2, {".bz2", "bzip2 compressed file"}},
    {FileExtension::XZ, {".xz", "xz compressed file"}},
    {FileExtension::ISO, {".iso", "optical image / installer image"}},
    {FileExtension::HTML, {".html", "web page"}},
    {FileExtension::CSS, {".css", "Cascading Style Sheets"}},
    {FileExtension::JPG, {".jpg", "JPEG image"}},
    {FileExtension::PNG, {".png", "PNG image"}},
    {FileExtension::GIF, {".gif", "GIF image"}},
    {FileExtension::SVG, {".svg", "SVG vector image"}},
    {FileExtension::WEBP, {".webp", "WEBP image"}},
    {FileExtension::MP3, {".mp3", "MP3 audio"}},
    {FileExtension::WAV, {".wav", "WAV audio"}},
    {FileExtension::FLAC, {".flac", "FLAC audio"}},
    {FileExtension::MP4, {".mp4", "MP4 video"}},
    {FileExtension::MKV, {".mkv", "Matroska video"}},
    {FileExtension::PDF, {".pdf", "Portable Document Format"}},
    {FileExtension::DOCX, {".docx", "Microsoft Word document"}},
    {FileExtension::XLSX, {".xlsx", "Microsoft Excel spreadsheet"}},
    {FileExtension::PPTX, {".pptx", "Microsoft PowerPoint presentation"}},
    {FileExtension::PEM, {".pem", "PEM formatted certificate/key"}},
    {FileExtension::CRT, {".crt", "certificate file"}},
    {FileExtension::KEY, {".key", "private key file"}},
    {FileExtension::MD5, {".md5", "MD5 checksum file"}},
    {FileExtension::SHA1, {".sha1", "SHA-1 checksum file"}},
    {FileExtension::SHA256, {".sha256", "SHA-256 checksum file"}},
    {FileExtension::UNKNOWN, {"", ""}}};

// helpers using the hash table
static inline std::string FileExtToString(const FileExtension& fe) noexcept {
  auto it = FileExtTable.find(fe);
  if (it == FileExtTable.end()) return {};
  return it->second.first;
}

static inline std::string FileExtToDescription(const FileExtension& fe) noexcept {
  auto it = FileExtTable.find(fe);
  if (it == FileExtTable.end()) return {};
  return it->second.second;
}

// reverse lookup: find enum by extension string (exact match, case-sensitive)
static inline FileExtension FileExtFromString(const std::string& ext) noexcept {
  for (const auto& kv : FileExtTable) {
    if (kv.second.first == ext) return kv.first;
  }
  return FileExtension::UNKNOWN;
}

#endif  // SRC_EXAMPLE_IPERF_BATS_BATS_FILE_H_
