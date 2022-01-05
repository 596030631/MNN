package com.taobao.android.mnndemo;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Matrix;
import android.hardware.SensorManager;
import android.hardware.usb.UsbDevice;
import android.media.CamcorderProfile;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.util.Log;
import android.view.OrientationEventListener;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewStub;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import com.android.uvccamera.IFrameCallback;
import com.android.uvccamera.USBMonitor;
import com.android.uvccamera.UVCCamera;
import com.taobao.android.mnn.MNNForwardType;
import com.taobao.android.mnn.MNNImageProcess;
import com.taobao.android.mnn.MNNNetInstance;
import com.taobao.android.utils.Common;
import com.taobao.android.utils.TxtFileReader;

import java.nio.ByteBuffer;
import java.text.DecimalFormat;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;

public class VideoActivity extends AppCompatActivity implements AdapterView.OnItemSelectedListener {

    private final String TAG = "VideoActivity";
    private final int MAX_CLZ_SIZE = 1000;

    private final String MobileModelFileName = "MobileNet/v2/mobilenet_v2.caffe.mnn";
    private final String MobileWordsFileName = "MobileNet/synset_words.txt";

    private final String NanodetlFileName = "NanodetNet/nanodet-320.mnn";
    private final String NanodetWordsFileName = "NanodetNet/nanodet.txt";

    private final String SqueezeModelFileName = "SqueezeNet/v1.1/squeezenet_v1.1.caffe.mnn";
    private final String SqueezeWordsFileName = "SqueezeNet/squeezenet.txt";

    private String mMobileModelPath;
    private String mNanodetModelPath;
    private String mSqueezeModelPath;

    private List<String> mMobileTaiWords;
    private List<String> mNanodetTaiWords;
    private List<String> mSqueezeTaiWords;

    private int mSelectedModelIndex;// current using modle
    private final MNNNetInstance.Config mConfig = new MNNNetInstance.Config();// session config

    private Spinner mForwardTypeSpinner;
    private Spinner mThreadNumSpinner;
    private Spinner mModelSpinner;
    private Spinner mMoreDemoSpinner;

    private TextView mFirstResult;
    private TextView mSecondResult;
    private TextView mThirdResult;
    private TextView mTimeTextView;

    private final int MobileInputWidth = 224;
    private final int MobileInputHeight = 224;

    private final int SqueezeInputWidth = 227;
    private final int SqueezeInputHeight = 227;

    HandlerThread mThread;
    Handler mHandle;

    private UVCCamera camera;
    private final AtomicBoolean mLockUIRender = new AtomicBoolean(false);
    private final AtomicBoolean mDrop = new AtomicBoolean(false);

    private MNNNetInstance mNetInstance;
    private MNNNetInstance.Session mSession;
    private MNNNetInstance.Session.Tensor mInputTensor;
    private boolean selectCamera = false;

    private final USBMonitor.OnDeviceConnectListener onDeviceConnectListener = new USBMonitor.OnDeviceConnectListener() {
        @Override
        public void onAttach(UsbDevice device) {
            if ((device.getProductId() == 0x1001 && device.getVendorId() == 0x1d6c) || (device.getProductId() == 0x636b && device.getVendorId() == 0xc45)) {
                Log.d(TAG, "请求相机权限");
                usbMonitor.requestPermission(device);
            } else {
                Log.d(TAG, "不符合相机标号");
            }
        }

        @Override
        public void onDetach(UsbDevice device) {

        }

        @Override
        public void onConnect(UsbDevice device, USBMonitor.UsbControlBlock ctrlBlock, boolean createNew) {
            if (selectCamera) return;
            selectCamera = true;
            camera = new UVCCamera();
            camera.open(ctrlBlock);
            camera.setPreviewDisplay(mCameraView.getHolder());
            camera.setPreviewSize(640, 480, UVCCamera.FRAME_FORMAT_YUYV);
            camera.startPreview();

            handlePreViewCallBack();
        }

        @Override
        public void onDisconnect(UsbDevice device, USBMonitor.UsbControlBlock ctrlBlock) {

        }

        @Override
        public void onCancel(UsbDevice device) {

        }
    };
    private USBMonitor usbMonitor;
    private SurfaceView mCameraView;

    private void prepareModels() {

        mMobileModelPath = getCacheDir() + "mobilenet_v1.caffe.mnn";
        try {
            Common.copyAssetResource2File(getBaseContext(), MobileModelFileName, mMobileModelPath);
            mMobileTaiWords = TxtFileReader.getUniqueUrls(getBaseContext(), MobileWordsFileName, Integer.MAX_VALUE);
        } catch (Throwable e) {
            throw new RuntimeException(e);
        }

        mNanodetModelPath = getCacheDir() + "nanodet-320.mnn";
        try {
            Common.copyAssetResource2File(getBaseContext(), NanodetlFileName, mNanodetModelPath);
            mNanodetTaiWords = TxtFileReader.getUniqueUrls(getBaseContext(), NanodetWordsFileName, Integer.MAX_VALUE);
        } catch (Throwable e) {
            throw new RuntimeException(e);
        }

        mSqueezeModelPath = getCacheDir() + "squeezenet_v1.1.caffe.mnn";
        try {
            Common.copyAssetResource2File(getBaseContext(), SqueezeModelFileName, mSqueezeModelPath);
            mSqueezeTaiWords = TxtFileReader.getUniqueUrls(getBaseContext(), SqueezeWordsFileName, Integer.MAX_VALUE);
        } catch (Throwable e) {
            throw new RuntimeException(e);
        }
    }


    private void prepareNet() {
        if (null != mSession) {
            mSession.release();
            mSession = null;
        }
        if (mNetInstance != null) {
            mNetInstance.release();
            mNetInstance = null;
        }

        String modelPath = mMobileModelPath;
        if (mSelectedModelIndex == 0) {
            modelPath = mMobileModelPath;
        } else if (mSelectedModelIndex == 1) {
            modelPath = mNanodetModelPath;
        } else if (mSelectedModelIndex == 2) {
            modelPath = mSqueezeModelPath;
        }

        // create net instance
        mNetInstance = MNNNetInstance.createFromFile(modelPath);

        // mConfig.saveTensors;
        mSession = mNetInstance.createSession(mConfig);

        // get input tensor
        mInputTensor = mSession.getInput(null);

        int[] dimensions = mInputTensor.getDimensions();
        dimensions[0] = 1; // force batch = 1  NCHW  [batch, channels, height, width]
        mInputTensor.reshape(dimensions);
        mSession.reshape();

        mLockUIRender.set(false);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
                WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        setContentView(R.layout.activity_main);

        mCameraView = findViewById(R.id.camera_view_abc);

        mSelectedModelIndex = 0;
        mConfig.numThread = 4;
        mConfig.forwardType = MNNForwardType.FORWARD_CPU.type;

        // prepare mnn net models
        prepareModels();

        mForwardTypeSpinner = (Spinner) findViewById(R.id.forwardTypeSpinner);
        mThreadNumSpinner = (Spinner) findViewById(R.id.threadsSpinner);
        mThreadNumSpinner.setSelection(3);
        mModelSpinner = (Spinner) findViewById(R.id.modelTypeSpinner);
        mMoreDemoSpinner = (Spinner) findViewById(R.id.MoreDemo);

        mFirstResult = findViewById(R.id.firstResult);
        mSecondResult = findViewById(R.id.secondResult);
        mThirdResult = findViewById(R.id.thirdResult);
        mTimeTextView = findViewById(R.id.timeTextView);

        mForwardTypeSpinner.setOnItemSelectedListener(VideoActivity.this);
        mThreadNumSpinner.setOnItemSelectedListener(VideoActivity.this);
        mModelSpinner.setOnItemSelectedListener(VideoActivity.this);
        mMoreDemoSpinner.setOnItemSelectedListener(VideoActivity.this);

        // init sub thread handle
        mLockUIRender.set(true);
        clearUIForPrepareNet();


        mThread = new HandlerThread("MNNNet");
        mThread.start();
        mHandle = new Handler(mThread.getLooper());

        mHandle.post(new Runnable() {
            @Override
            public void run() {
                prepareNet();
            }
        });


        mHandle.postDelayed(new Runnable() {
            @Override
            public void run() {
                usbMonitor = new USBMonitor(VideoActivity.this);
                usbMonitor.setOnDeviceConnectListener(onDeviceConnectListener);
                usbMonitor.register();
            }
        }, 5_000);
    }


    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    private static byte[] getBufferBytes(ByteBuffer frame) {
        int len = frame.limit() - frame.position();
        byte[] bytes1 = new byte[len];
        frame.get(bytes1);
        return bytes1;
    }

    private void handlePreViewCallBack() {


        camera.setFrameCallback(frame -> {
            Log.d(TAG, "_");

            if (mLockUIRender.get()) {
                return;
            }


            if (mDrop.get()) {
                Log.w(TAG, "drop frame , net running too slow !!");
            } else {
                mDrop.set(true);
                mHandle.post(() -> {
                    mDrop.set(false);
                    if (mLockUIRender.get()) {
                        return;
                    }

                    // calculate corrected angle based on camera orientation and mobile rotate degree. (back camrea)
                    int needRotateAngle = (0) % 360;

                    /*
                     *  convert data to input tensor
                     */
                    final MNNImageProcess.Config config = new MNNImageProcess.Config();
                    if (mSelectedModelIndex == 0) {
                        // normalization params
                        config.mean = new float[]{103.94f, 116.78f, 123.68f};
                        config.normal = new float[]{0.017f, 0.017f, 0.017f};
                        config.source = MNNImageProcess.Format.YUV_NV21;// input source format
                        config.dest = MNNImageProcess.Format.BGR;// input data format

                        // matrix transform: dst to src
                        Matrix matrix = new Matrix();
                        matrix.postScale(MobileInputWidth / (float) 640, MobileInputHeight / (float) 480);
                        matrix.postRotate(needRotateAngle, MobileInputWidth / 2, MobileInputHeight / 2);
                        matrix.invert(matrix);

                        MNNImageProcess.convertBuffer(getBufferBytes(frame), 640, 480, mInputTensor, config, matrix);

                    } else if (mSelectedModelIndex == 1) {
                        // input data format
                        config.source = MNNImageProcess.Format.YUV_NV21;// input source format
                        config.dest = MNNImageProcess.Format.BGR;// input data format

                        // matrix transform: dst to src
                        final Matrix matrix = new Matrix();
                        matrix.postScale(SqueezeInputWidth / (float) (float) 640, SqueezeInputHeight / (float) 480);
                        matrix.postRotate(needRotateAngle, SqueezeInputWidth / 2, SqueezeInputWidth / 2);
                        matrix.invert(matrix);

                        MNNImageProcess.convertBuffer(getBufferBytes(frame), 640, 480, mInputTensor, config, matrix);
                    }

                    final long startTimestamp = System.nanoTime();
                    /**
                     * inference
                     */
                    mSession.run();

                    /**
                     * get output tensor
                     */
                    MNNNetInstance.Session.Tensor output = mSession.getOutput(null);

                    float[] result = output.getFloatData();// get float results
                    final long endTimestamp = System.nanoTime();
                    final float inferenceTimeCost = (endTimestamp - startTimestamp) / 1000000.0f;

                    if (result.length > MAX_CLZ_SIZE) {
                        Log.w(TAG, "session result too big (" + result.length + "), model incorrect ?");
                    }

                    final List<Map.Entry<Integer, Float>> maybes = new ArrayList<>();
                    for (int i = 0; i < result.length; i++) {
                        float confidence = result[i];
                        if (confidence > 0.01) {
                            maybes.add(new AbstractMap.SimpleEntry<Integer, Float>(i, confidence));
                        }
                    }

                    Collections.sort(maybes, new Comparator<Map.Entry<Integer, Float>>() {
                        @Override
                        public int compare(Map.Entry<Integer, Float> o1, Map.Entry<Integer, Float> o2) {
                            if (Math.abs(o1.getValue() - o2.getValue()) <= Float.MIN_NORMAL) {
                                return 0;
                            }
                            return o1.getValue() > o2.getValue() ? -1 : 1;
                        }
                    });

                    // show results on ui
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {

                            if (maybes.size() == 0) {
                                mFirstResult.setText("no data");
                                mSecondResult.setText("");
                                mThirdResult.setText("");
                            }
                            if (maybes.size() > 0) {
                                mFirstResult.setTextColor(maybes.get(0).getValue() > 0.2 ? Color.BLACK : Color.parseColor("#a4a4a4"));
                                final Integer iKey = maybes.get(0).getKey();
                                final Float fValue = maybes.get(0).getValue();
                                String strWord = "unknown";
                                if (0 == mSelectedModelIndex) {
                                    if (iKey < mMobileTaiWords.size()) {
                                        strWord = mMobileTaiWords.get(iKey);
                                    }
                                } else {
                                    if (iKey < mSqueezeTaiWords.size()) {
                                        strWord = mSqueezeTaiWords.get(iKey);
                                    }
                                }
                                final String resKey = mSelectedModelIndex == 1 ? strWord.length() >= 10 ? strWord.substring(10) : strWord : strWord;
                                mFirstResult.setText(resKey + "：" + new DecimalFormat("0.00").format(fValue));

                            }
                            if (maybes.size() > 1) {
                                final Integer iKey = maybes.get(1).getKey();
                                final Float fValue = maybes.get(1).getValue();
                                String strWord = "unknown";
                                if (0 == mSelectedModelIndex) {
                                    if (iKey < mMobileTaiWords.size()) {
                                        strWord = mMobileTaiWords.get(iKey);
                                    }
                                } else {
                                    if (iKey < mSqueezeTaiWords.size()) {
                                        strWord = mSqueezeTaiWords.get(iKey);
                                    }
                                }
                                final String resKey = mSelectedModelIndex == 1 ? strWord.length() >= 10 ? strWord.substring(10) : strWord : strWord;
                                mSecondResult.setText(resKey + "：" + new DecimalFormat("0.00").format(fValue));

                            }
                            if (maybes.size() > 2) {
                                final Integer iKey = maybes.get(2).getKey();
                                final Float fValue = maybes.get(2).getValue();
                                String strWord = "unknown";
                                if (0 == mSelectedModelIndex) {
                                    if (iKey < mMobileTaiWords.size()) {
                                        strWord = mMobileTaiWords.get(iKey);
                                    }
                                } else {
                                    if (iKey < mSqueezeTaiWords.size()) {
                                        strWord = mSqueezeTaiWords.get(iKey);
                                    }
                                }
                                final String resKey = mSelectedModelIndex == 1 ? strWord.length() >= 10 ? strWord.substring(10) : strWord : strWord;
                                mThirdResult.setText(resKey + "：" + new DecimalFormat("0.00").format(fValue));
                            }

                            mTimeTextView.setText("cost time：" + inferenceTimeCost + "ms");
                        }
                    });

                });
            }


        }, UVCCamera.PIXEL_FORMAT_NV21);


    }


    @Override
    public void onItemSelected(AdapterView<?> adapterView, View view, int i, long l) {

        // forward type
        if (mForwardTypeSpinner.getId() == adapterView.getId()) {

            if (i == 0) {
                mConfig.forwardType = MNNForwardType.FORWARD_CPU.type;
            } else if (i == 1) {
                mConfig.forwardType = MNNForwardType.FORWARD_OPENCL.type;
            } else if (i == 2) {
                mConfig.forwardType = MNNForwardType.FORWARD_OPENGL.type;
            } else if (i == 3) {
                mConfig.forwardType = MNNForwardType.FORWARD_VULKAN.type;
            }
        }
        // threads num
        else if (mThreadNumSpinner.getId() == adapterView.getId()) {

            String[] threadList = getResources().getStringArray(R.array.thread_list);
            mConfig.numThread = Integer.parseInt(threadList[i].split(" ")[1]);
        }
        // model index
        else if (mModelSpinner.getId() == adapterView.getId()) {

            mSelectedModelIndex = i;
        } else if (mMoreDemoSpinner.getId() == adapterView.getId()) {

            if (i == 1) {
                Intent intent = new Intent(VideoActivity.this, ImageActivity.class);
                startActivity(intent);
            } else if (i == 2) {
                Intent intent = new Intent(VideoActivity.this, PortraitActivity.class);
                startActivity(intent);
            } else if (i == 3) {
                Intent intent = new Intent(VideoActivity.this, OpenGLTestActivity.class);
                startActivity(intent);
            }
        }


        mLockUIRender.set(true);
        clearUIForPrepareNet();

        mHandle.post(new Runnable() {
            @Override
            public void run() {
                prepareNet();
            }
        });

    }

    private void clearUIForPrepareNet() {
        mFirstResult.setText("prepare net ...");
        mSecondResult.setText("");
        mThirdResult.setText("");
        mTimeTextView.setText("");
    }


    @Override
    public void onNothingSelected(AdapterView<?> adapterView) {

    }

    @Override
    protected void onPause() {
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
    }


    @Override
    protected void onDestroy() {
        mThread.interrupt();

        /**
         * instance release
         */
        mHandle.post(new Runnable() {
            @Override
            public void run() {
                if (mNetInstance != null) {
                    mNetInstance.release();
                }
            }
        });

        camera.close();
        camera.destroy();

        super.onDestroy();
    }
}