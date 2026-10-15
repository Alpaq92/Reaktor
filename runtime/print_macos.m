#import <Cocoa/Cocoa.h>
#import <PDFKit/PDFKit.h>

/* The PDF through the system's print dialog. */
int
reaktor_print_macos(const char *path)
{
    PDFDocument      *doc;
    NSPrintOperation *op;

    doc = [[PDFDocument alloc] initWithURL:[NSURL fileURLWithPath:
                                               [NSString stringWithUTF8String:path]]];
    if (!doc) return 0;
    op = [doc printOperationForPrintInfo:[NSPrintInfo sharedPrintInfo]
                             scalingMode:kPDFPrintPageScaleToFit
                              autoRotate:YES];
    return op && [op runOperation] ? 1 : 0;
}
