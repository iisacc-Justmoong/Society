import com.google.zxing.BinaryBitmap;
import com.google.zxing.RGBLuminanceSource;
import com.google.zxing.ReaderException;
import com.google.zxing.common.HybridBinarizer;
import com.google.zxing.qrcode.QRCodeReader;
import java.awt.image.BufferedImage;
import java.io.File;
import javax.imageio.ImageIO;

// Exercise the exact decoder shipped with the Android camera against a Qt-rendered frame.
public final class DecodeAndroidQr {
    private static String decode(BufferedImage image) throws ReaderException {
        int width = image.getWidth(), height = image.getHeight();
        int[] pixels = image.getRGB(0, 0, width, height, null, 0, width);
        return new QRCodeReader().decode(new BinaryBitmap(new HybridBinarizer(
            new RGBLuminanceSource(width, height, pixels)))).getText();
    }
    public static void main(String[] args) throws Exception {
        BufferedImage image = ImageIO.read(new File(args[0]));
        String text = decode(image);
        if (!text.startsWith("society://pair?") || !text.contains("v=2") || text.contains("relay="))
            throw new AssertionError("Expected the desktop local-network pairing QR");
        BufferedImage blank = new BufferedImage(image.getWidth(), image.getHeight(), BufferedImage.TYPE_INT_RGB);
        try { decode(blank); throw new AssertionError("A blank frame must not yield a pairing code"); }
        catch (ReaderException expected) { }
        System.out.println("Android ZXing decoder read the actual desktop QR; blank-frame rejection passed.");
    }
}
