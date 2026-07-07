#include <QApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QImage>
#include <QImageWriter>
#include <QImageReader>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QIODevice>
#include <QByteArray>
#include <QList>

#include <libheif/heif.h>

#include <iostream>
#include <string>


// format configuration object
struct OutputFormat {
    QString label;
    QString extension;
    QByteArray qtFormat;
    int quality;
};

/*
 * Method that determines if we can actually save/write images in this format on this machine?
 * So, before offering JPEG or PNG in the app, we check first if Qt actually supports writing that format.
 * here, the method is static, since we'll only be using it in this .cpp, nowhere else
*/
static bool qtSupportsWritingFormat(const QByteArray& format)
{
    //which formats can you currenlty write? It might return smth like: ["bmp", "jpg", "jpeg", "png", "ppm", "xbm", "xpm"]
    QList<QByteArray> supportedFormats = QImageWriter::supportedImageFormats();

    QByteArray requested = format.toLower();

    // loop through all supported formats, for each we compare it with requested format
    // returns true -> we can write in this format
    for (const QByteArray& supported : supportedFormats) {
        if (supported.toLower() == requested) {
            return true;
        }
    }

    // Qt may expose JPEG as "jpg" or "jpeg", depending on platform/plugins.
    if (requested == "jpeg") {
        for (const QByteArray& supported : supportedFormats) {
            QByteArray normalized = supported.toLower();
            if (normalized == "jpg" || normalized == "jpeg") {
                return true;
            }
        }
    }

    return false;
}

static QList<OutputFormat> getAvailableOutputFormats()
{
    QList<OutputFormat> formats;

    if (qtSupportsWritingFormat("JPEG") || qtSupportsWritingFormat("JPG")) {
        formats.append({
            "JPEG (.jpg)",
            "jpg",
            "JPEG",
            95
        });
    }

    if (qtSupportsWritingFormat("PNG")) {
        formats.append({
            "PNG (.png)",
            "png",
            "PNG",
            -1
        });
    }

    return formats;
}


static OutputFormat chooseOutputFormat(bool& ok)
{
    QList<OutputFormat> availableFormats = getAvailableOutputFormats();

    if (availableFormats.isEmpty()) {
        ok = false;
        return {};
    }

    QStringList options;

    for (const OutputFormat& format : availableFormats) {
        options << format.label;
    }

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
        ok = false;
        return {};
    }

    for (const OutputFormat& format : availableFormats) {
        if (format.label == choice) {
            return format;
        }
    }

    ok = false;
    return {};
}


static bool isHeicFile(const QString& path)
{
    QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "heic" || suffix == "heif";
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


static bool verifySavedImage(
    const QString& outputPath,
    int expectedWidth,
    int expectedHeight,
    std::string& errorMessage
)
{
    QImageReader reader(outputPath);

    if (!reader.canRead()) {
        errorMessage = "Output file was written, but Qt cannot read it back.";
        return false;
    }

    QSize savedSize = reader.size();

    if (savedSize.width() != expectedWidth || savedSize.height() != expectedHeight) {
        errorMessage =
            "Output image dimensions do not match decoded image dimensions.";
        return false;
    }

    return true;
}


static bool saveImageAtomically(
    const QImage& image,
    const QString& outputPath,
    const OutputFormat& outputFormat,
    std::string& errorMessage
)
{
    if (image.isNull()) {
        errorMessage = "Decoded image is empty.";
        return false;
    }

    QImage imageToSave = image;

    /*
       JPEG does not support transparency.
       PNG can keep alpha.
    */
    if (outputFormat.qtFormat.toUpper() == "JPEG" ||
        outputFormat.qtFormat.toUpper() == "JPG") {
        imageToSave = image.convertToFormat(QImage::Format_RGB888);
    }

    QSaveFile saveFile(outputPath);

    if (!saveFile.open(QIODevice::WriteOnly)) {
        errorMessage = saveFile.errorString().toStdString();
        return false;
    }

    QImageWriter writer(&saveFile, outputFormat.qtFormat);

    if (outputFormat.quality >= 0) {
        writer.setQuality(outputFormat.quality);
    }

    if (!writer.write(imageToSave)) {
        errorMessage = writer.errorString().toStdString();
        saveFile.cancelWriting();
        return false;
    }

    if (!saveFile.commit()) {
        errorMessage = saveFile.errorString().toStdString();
        return false;
    }

    return verifySavedImage(
        outputPath,
        imageToSave.width(),
        imageToSave.height(),
        errorMessage
    );
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
        errorMessage =
            error.message ? error.message : "Could not get primary image handle.";
        heif_context_free(context);
        return false;
    }

    heif_image* decodedImage = nullptr;

    /*
       Decode as RGBA.
       PNG can preserve alpha.
       JPEG will be converted to RGB before saving.
    */
    error = heif_decode_image(
        handle,
        &decodedImage,
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

    const uint8_t* pixelData = heif_image_get_plane_readonly(
        decodedImage,
        heif_channel_interleaved,
        &stride
    );

    if (!pixelData) {
        errorMessage = "Could not access decoded image pixel data.";
        heif_image_release(decodedImage);
        heif_image_handle_release(handle);
        heif_context_free(context);
        return false;
    }

    int width = heif_image_get_width(decodedImage, heif_channel_interleaved);
    int height = heif_image_get_height(decodedImage, heif_channel_interleaved);

    if (width <= 0 || height <= 0) {
        errorMessage = "Decoded image has invalid dimensions.";
        heif_image_release(decodedImage);
        heif_image_handle_release(handle);
        heif_context_free(context);
        return false;
    }

    QImage qImage(
        pixelData,
        width,
        height,
        stride,
        QImage::Format_RGBA8888
    );

    /*
       Important:
       qImage points to memory owned by libheif.
       copy() makes Qt own the pixels before libheif memory is released.
    */
    QImage ownedImage = qImage.copy();

    bool saved = saveImageAtomically(
        ownedImage,
        outputPath,
        outputFormat,
        errorMessage
    );

    heif_image_release(decodedImage);
    heif_image_handle_release(handle);
    heif_context_free(context);

    return saved;
}


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QList<OutputFormat> availableFormats = getAvailableOutputFormats();

    if (availableFormats.isEmpty()) {
        QMessageBox::critical(
            nullptr,
            "HEIC Image Converter",
            "No supported output formats were found.\n\n"
            "Qt cannot write JPEG or PNG on this system."
        );

        return 1;
    }

    QStringList selectedFiles = QFileDialog::getOpenFileNames(
        nullptr,
        "Select HEIC/HEIF images",
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
    int skippedCount = 0;

    QString report;

    for (const QString& filePath : selectedFiles) {
        if (!isHeicFile(filePath)) {
            skippedCount++;
            report += "Skipped unsupported file:\n";
            report += filePath + "\n\n";
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
        "Failed: " + QString::number(failedCount) + "\n"
        "Skipped: " + QString::number(skippedCount) + "\n\n" +
        report;

    QMessageBox::information(
        nullptr,
        "HEIC Image Converter",
        summary
    );

    std::cout << summary.toStdString() << std::endl;

    return failedCount > 0 ? 1 : 0;
}