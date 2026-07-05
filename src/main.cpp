#include <QApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QImage>
#include <QImageWriter>
#include <QStringList>
#include <QFileInfo>
#include <QDir>

#include <libheif/heif.h>

#include <iostream>
#include <string>


struct OutputFormat {
    QString label;
    QString extension;
    QByteArray qtFormat;
    int quality;
};


static OutputFormat chooseOutputFormat(bool& ok)
{
    QStringList options;
    options << "JPEG (.jpg)" << "PNG (.png)";

    QString choice = QInputDialog::getItem(
        nullptr,
        "Choose output format",
        "Convert HEIC images to:",
        options,
        0,
        false,
        &ok
    );

    if (!ok || choice.isEmpty()) {
        return {};
    }

    if (choice.startsWith("PNG")) {
        return {
            "PNG",
            "png",
            "PNG",
            -1
        };
    }

    return {
        "JPEG",
        "jpg",
        "JPEG",
        95
    };
}


static QString makeOutputPath(const QString& inputPath, const QString& extension)
{
    QFileInfo info(inputPath);

    QString directory = info.absolutePath();
    QString baseName = info.completeBaseName();

    QString outputPath = QDir(directory).filePath(baseName + "." + extension);

    int counter = 1;
    while (QFileInfo::exists(outputPath)) {
        outputPath = QDir(directory).filePath(
            baseName + "_" + QString::number(counter) + "." + extension
        );
        counter++;
    }

    return outputPath;
}


static bool isHeicFile(const QString& path)
{
    QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "heic" || suffix == "heif";
}


static bool saveImage(
    const QImage& image,
    const QString& outputPath,
    const OutputFormat& outputFormat,
    std::string& errorMessage
)
{
    QImage imageToSave = image;

    /*
       JPEG does not support transparency.
       PNG can keep alpha, but JPEG should be plain RGB.
    */
    if (outputFormat.qtFormat == "JPEG") {
        imageToSave = image.convertToFormat(QImage::Format_RGB888);
    }

    QImageWriter writer(outputPath, outputFormat.qtFormat);

    if (outputFormat.quality >= 0) {
        writer.setQuality(outputFormat.quality);
    }

    bool saved = writer.write(imageToSave);

    if (!saved) {
        errorMessage = writer.errorString().toStdString();
        return false;
    }

    return true;
}


static bool convertHeicToImage(
    const QString& inputPath,
    const OutputFormat& outputFormat,
    QString& outputPath,
    std::string& errorMessage
)
{
    outputPath = makeOutputPath(inputPath, outputFormat.extension);

    heif_context* context = heif_context_alloc();
    if (!context) {
        errorMessage = "Could not allocate HEIF context.";
        return false;
    }

    heif_error error = heif_context_read_from_file(
        context,
        inputPath.toStdString().c_str(),
        nullptr
    );

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

    /*
       Decode as RGBA so PNG can preserve alpha if present.
       For JPEG, we later convert to RGB before saving.
    */
    error = heif_decode_image(
        handle,
        &image,
        heif_colorspace_RGB,
        heif_chroma_interleaved_RGBA,
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
        QImage::Format_RGBA8888
    );

    /*
       qImage points to libheif-owned memory.
       copy() makes Qt own the pixels before libheif releases them.
    */
    QImage ownedImage = qImage.copy();

    bool saved = saveImage(
        ownedImage,
        outputPath,
        outputFormat,
        errorMessage
    );

    heif_image_release(image);
    heif_image_handle_release(handle);
    heif_context_free(context);

    return saved;
}


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QStringList selectedFiles = QFileDialog::getOpenFileNames(
        nullptr,
        "Select HEIC images",
        QDir::homePath(),
        "HEIC/HEIF Images (*.heic *.HEIC *.heif *.HEIF)"
    );

    if (selectedFiles.isEmpty()) {
        std::cout << "No files selected.\n";
        return 0;
    }

    bool formatChosen = false;
    OutputFormat outputFormat = chooseOutputFormat(formatChosen);

    if (!formatChosen) {
        std::cout << "No output format selected.\n";
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

        QString outputPath;
        std::string errorMessage;

        bool success = convertHeicToImage(
            filePath,
            outputFormat,
            outputPath,
            errorMessage
        );

        if (success) {
            convertedCount++;
            report += "Converted:\n";
            report += filePath + "\n";
            report += "→ " + outputPath + "\n\n";
        } else {
            failedCount++;
            report += "Failed:\n";
            report += filePath + "\n";
            report += "Reason: " + QString::fromStdString(errorMessage) + "\n\n";
        }
    }

    QString summary =
        "Conversion complete.\n\n"
        "Output format: " + outputFormat.label + "\n"
        "Converted: " + QString::number(convertedCount) + "\n"
        "Failed: " + QString::number(failedCount) + "\n\n" +
        report;

    QMessageBox::information(nullptr, "HEIC Image Converter", summary);

    std::cout << summary.toStdString() << std::endl;

    return 0;
}