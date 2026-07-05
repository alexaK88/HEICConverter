#include <string>
#include <vector>
#include  <iostream>

#include <QDir>
#include <QImage>
#include <QFileInfo>
#include <QStringList>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>

#include <libheif/heif.h> //heif and avif file format decoder and encoder


using namespace std;

static std::string makeOutputPath(const QString& inputPath) {
    QFileInfo info(inputPath);

    QString directory = info.absolutePath();
    QString baseName = info.completeBaseName();

    QString outputPath = QDir(directory).filePath(baseName + ".jpg");

    while (QFileInfo::exists(outputPath)) {
        outputPath = QDir(directory).filePath(baseName + ".jpg");
    }

    return outputPath.toStdString();
}

static bool isHeicFile(const QString& path) {
    QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "heic" || suffix =="heif";
}

static bool convertHeicToJpeg(const QString& inputPath, std::string& outputPath, std::string& errorMessage) {
    outputPath = makeOutputPath(inputPath);
    heif_context* context = heif_context_alloc();

    if (!context) {
        errorMessage = "Could not allcoate HEIF context.";
        return false;
    }

    heif_error error = heif_context_read_from_file(
        context, inputPath.toStdString().c_str(), nullptr);

    if (error.code != heif_error_Ok) {
        errorMessage = error.message ? error.message : "Could not read HEIC file.";
        heif_context_free(context);
        return false;
    }

    heif_image_handle* handle = nullptr;
    error = heif_context_get_primary_image_handle(context, &handle);

    if (error.code != heif_error_Ok) {
        errorMessage = error.message ? error.message : "Could not get primary image.";
        heif_context_free(context);
        return false;
    }

    heif_image* image = nullptr;
    error = heif_decode_image(
        handle,
        &image,
        heif_colorspace_RGB,
        heif_chroma_interleaved_RGB,
        nullptr
    );

    if (error.code != heif_error_Ok) {
        errorMessage = error.message ? error.message : "Could not decode HEIC image.";
        heif_image_handle_release(handle);
        heif_context_free(context);
        return false;
    }

    int stride = 0;
    const uint8_t* data = heif_image_get_plane_readonly(
        image,
        heif_channel_interleaved,
        &stride
    );

    if (!data) {
        errorMessage = "Could not access decoded image pixels.";
        heif_image_release(image);
        heif_image_handle_release(handle);
        heif_context_free(context);
        return false;
    }

    int width = heif_image_get_width(image, heif_channel_interleaved);
    int height = heif_image_get_height(image, heif_channel_interleaved);

    QImage qImage(
        data,
        width,
        height,
        stride,
        QImage::Format_RGB888
    );

    /*
       Important:
       qImage currently points to libheif-owned memory.
       copy() makes Qt own the pixel data before we release libheif objects.
    */
    QImage ownedImage = qImage.copy();

    bool saved = ownedImage.save(QString::fromStdString(outputPath), "JPEG", 95);

    heif_image_release(image);
    heif_image_handle_release(handle);
    heif_context_free(context);

    if (!saved) {
        errorMessage = "Could not save JPEG file.";
        return false;
    }

    return true;
}


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QStringList selectedFiles = QFileDialog::getOpenFileNames(
        nullptr,
        "Select HEIC image(s)",
        QDir::homePath(),
        "HEIC/HEIF Images (*.heic *.HEIC *.heif *.HEIF)"
    );

    if (selectedFiles.isEmpty()) {
        std::cout << "No files selected.\n";
        return 0;
    }

    int convertedCount = 0;
    int failedCount = 0;

    QString report;

    for (const QString& filePath : selectedFiles) {
        if (!isHeicFile(filePath)) {
            report += "Skipped unsupported file: " + filePath + "\n";
            continue;
        }

        std::string outputPath;
        std::string errorMessage;

        bool success = convertHeicToJpeg(filePath, outputPath, errorMessage);

        if (success) {
            convertedCount++;
            report += "Converted:\n";
            report += filePath + "\n";
            report += "→ " + QString::fromStdString(outputPath) + "\n\n";
        } else {
            failedCount++;
            report += "Failed:\n";
            report += filePath + "\n";
            report += "Reason: " + QString::fromStdString(errorMessage) + "\n\n";
        }
    }

    QString summary =
        "Conversion complete.\n\n"
        "Converted: " + QString::number(convertedCount) + "\n"
        "Failed: " + QString::number(failedCount) + "\n\n" +
        report;

    QMessageBox::information(nullptr, "HEIC to JPEG Converter", summary);

    std::cout << summary.toStdString() << std::endl;

    return 0;
}