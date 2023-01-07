#include "sequence_viewer.h"
#include "pointcloud_processing.h"

SequenceViewer::SequenceViewer(
    std::string pcd_path, std::string img_path, std::string annot_path, std::string cameraparam_path, std::string cameraparam_save_path
) : pcd_len(0),
    current_pcd_id(0),
    img_path(img_path),
    annot_path(annot_path),
    cameraparam_save_path(cameraparam_save_path)
{
    cloud.reset(new PointCloudT);
    viewer.reset(new pcl::visualization::PCLVisualizer("3D Viewer"));

    load_pcd_files(pcd_path, ".pcd");

    load_point_cloud(pcd_files[current_pcd_id], cloud);
    apply_color(cloud);
    viewer->setBackgroundColor(1.0, 1.0, 1.0);
    viewer->addPointCloud<PointT>(cloud, "cloud");
    viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "cloud");

    viewer->addCoordinateSystem();
    viewer->addText(pcd_files[current_pcd_id], 0, 0, 0, 0, 0, "file_name");

    viewer->registerKeyboardCallback(keyboardEventOccurred, (void *)this);
    viewer->registerPointPickingCallback(pointPickingEventOccured, (void*)this); 

    update_annot(pcd_files[current_pcd_id]);

    if (!img_path.empty()) {
        img_viewer.reset(new pcl::visualization::ImageViewer("Image Viewer"));
    }
    update_image(pcd_files[current_pcd_id]);

    if (!cameraparam_path.empty())
    {
        load_camerapose(cameraparam_path);
    }
}

void SequenceViewer::load_pcd_files(const std::string file_path, std::string target_ext)
{
    namespace bfs = boost::filesystem;

    int file_check;
    std::string file_ext;
    bfs::path bfs_p(file_path);

    if (file_path.empty() or !bfs::exists(bfs_p))
    {
        std::string message = (boost::format("An argument '%1%' is empty or doesn't exist!") % file_path).str();
        throw std::runtime_error(message);
    }

    if (bfs::is_directory(bfs_p))
    {
        std::string pcd_file;
        bfs::directory_iterator end_itr;

        for (bfs::directory_iterator itr(bfs_p); itr != end_itr; ++itr)
        {
            if (bfs::is_regular_file(itr->path()))
            {
                pcd_file = itr->path().string();
                file_ext = bfs::extension(pcd_file);
                file_check = (file_ext == target_ext);
                if (file_check)
                {
                    std::cout << pcd_file << " is detected." << std::endl;
                    this->pcd_files.push_back(pcd_file);
                }
            }
        }
        std::sort(this->pcd_files.begin(), this->pcd_files.end());
    }
    else
    {
        file_ext = bfs::extension(file_path);
        file_check = (file_ext == target_ext);
        if (file_check)
        {
            std::cout << file_path << " is detected." << std::endl;
            this->pcd_files.push_back(file_path);
        }
    }

    this->pcd_len = this->pcd_files.size();
}

int SequenceViewer::load_point_cloud(const std::string pcd_file_path, PointCloudT::Ptr pcd_ptr)
{
    int load_status = pcl::io::loadPCDFile(pcd_file_path, *pcd_ptr);
    if (load_status != 0)
    {
        std::string message = (boost::format("Error : cannot load point cloud %1%") % pcd_file_path).str();
        throw std::runtime_error(message);
    }
    return load_status;
}

void SequenceViewer::update_cloud(int pcd_id)
{
    if (this->pcd_len == 1)
    {
        std::cout << "The number of loaded .pcd files is 1. The cloud shown in viewer would not change." << std::endl;
    }
    else
    {
        if (pcd_id >= this->pcd_len)
        {
            pcd_id = pcd_id % this->pcd_len;
        }

        this->current_pcd_id = pcd_id;
        std::string pcd_file = this->pcd_files[pcd_id];
        std::cout << "toggle cloud shown to : " << pcd_file << std::endl;

        PointCloudT::Ptr cloud_tmp(new PointCloudT);
        int load_status = load_point_cloud(pcd_file, cloud_tmp);

        if (load_status != 0)
        {
            std::cerr << "Error : cannot load point cloud " << pcd_file << std::endl;
        }
        else
        {
            apply_color(cloud_tmp);
            pcl::copyPointCloud(*cloud_tmp, *(this->cloud));
            this->viewer->updatePointCloud(this->cloud, "cloud");
            this->viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "cloud");

            // this->viewer->removeShape("center");
            this->viewer->removeAllShapes();
            this->viewer->addText(this->pcd_files[pcd_id], 0, 0, 0, 0, 0, "file_name");
            this->update_annot(pcd_file);

            this->update_image(pcd_file);
        }
    }
}

void SequenceViewer::load_image(std::string pcd_file_path, std::string target_ext) {
    namespace bfs = boost::filesystem;

    std::string img_p = this->img_path;
    bfs::path bfs_img_p (img_p);

    if (!bfs::exists(bfs_img_p))
    {
        std::cout << (boost::format("Warning : annotaion path '%1%' does not exist.") % img_p).str() << std::endl;
    }
    else 
    {
        bool ret = false;

        if (bfs::is_directory(bfs_img_p)) 
        {
            std::string file_name = bfs::path(pcd_file_path).stem().string();
            std::string img_file =  (bfs_img_p / (file_name + target_ext)).string();
            std::cout << "loading : " << img_file << std::endl;

            this->image = cv::imread(img_file);
        } 
        else if (bfs_img_p.extension().string() == target_ext)
        {
            this->image = cv::imread(img_p);
        }
    }
}

void SequenceViewer::update_image(const std::string pcd_file_path) {
    if (!this->img_path.empty()) 
    {
        load_image(pcd_file_path);

        unsigned width = this->image.cols;
        unsigned height = this->image.rows;
        unsigned channels = this->image.channels();

        // TODO : check if no error occurs because of static variable `data`.
        unsigned char data[width * height * channels];
        for (int h = 0; h < height; h++) {
            for (int w = 0; w < width; w++) {
                cv::Vec3b intensity = this->image.at<cv::Vec3b>(h, w);
                for (int c = 0; c < channels; c++) {
                    int addr_id = h * width * channels + w * channels + c;
                    uchar pixel_color   = intensity.val[channels - 1 - c];
                    data[addr_id] = pixel_color;
                }
            }
        }

        this->img_viewer->removeLayer("rgb_image");
        this->img_viewer->showRGBImage(data, width, height, "rgb_image", 1.0);
    }
}

void SequenceViewer::load_annot_json(std::string pcd_file_path)
{
    namespace bfs = boost::filesystem;

    std::string annot_p = this->annot_path;
    bfs::path bfs_annot_p (annot_p);

    this->bboxes.clear();

    if (!bfs::exists(bfs_annot_p))
    {
        std::cout << (boost::format("Warning : annotaion path '%1%' does not exist.") % annot_p).str() << std::endl;
    }
    else 
    {
        bool ret = false;

        if (bfs::is_directory(bfs_annot_p)) 
        {
            std::string file_name = bfs::path(pcd_file_path).stem().string();
            std::string annot_file =  (bfs_annot_p / (file_name + ".json")).string();
            std::cout << "loading : " << annot_file << std::endl;

            ret = load_annot(annot_file, this->bboxes);
        } 
        else if (bfs_annot_p.extension().string() == ".json")
        {
            ret = load_annot(annot_p, this->bboxes);
        }

        if (!ret)
        {
            std::cout << (boost::format("Warning : annotaion path '%1%' was skipped.") % annot_p).str() << std::endl;
            std::cout << "check file format." << std::endl;
        }
    }
}

void SequenceViewer::update_annot(std::string pcd_file_path) {
    if (!this->annot_path.empty())
    {
        load_annot_json(pcd_file_path);

        for (BBox3D bbox : this->bboxes) 
        {
            // if (bbox.id.find("person") != std::string::npos)
            // {
            //     std::cout << "load-bbox : " << bbox.id << std::endl;
            //     showBBox3D(bbox);
            // }
            std::cout << "load-bbox : " << bbox.id << " : " << std::endl;
            std::cout << "    (x, y, z) = (" << bbox.translation.x() << "," << bbox.translation.y() << "," << bbox.translation.z() << ")" << std::endl;
            std::cout << "    (w, h, d) = (" << bbox.width << "," << bbox.height << "," << bbox.depth << ")" << std::endl;
            std::cout << "    (w, x, y, z) = (" << bbox.rotation.w() << "," << bbox.rotation.x() << "," << bbox.rotation.y() << "," << bbox.rotation.z() << ")" << std::endl;
            this->showBBox3D(bbox);
        }
    }
}

void SequenceViewer::showBBox3D(const BBox3D &bbox)
{
    this->viewer->addCube(bbox.translation, bbox.rotation, bbox.width, bbox.depth, bbox.height, bbox.id);
    this->viewer->setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_REPRESENTATION, pcl::visualization::PCL_VISUALIZER_REPRESENTATION_WIREFRAME, bbox.id);
    this->viewer->setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, 1.0, 0.0, 0.0, bbox.id);
    this->viewer->setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_LINE_WIDTH, 2, bbox.id);

    std::string s = bbox.id + "_text";
    PointT trans(bbox.translation(0), bbox.translation(1), bbox.translation(2), 0, 0, 0);
    Eigen::Vector3f text_euler = bbox.rotation.toRotationMatrix().eulerAngles(0, 1, 2);
    double angle[3] = {(double)text_euler(0), (double)text_euler(1), (double)text_euler(2)};
    this->viewer->addText3D<PointT>(bbox.id, trans, angle, 1.0, 1.0, 1.0, 1.0, s);
}

void SequenceViewer::load_camerapose(std::string cameraparam_path)
{
    this->viewer->loadCameraParameters(cameraparam_path);
    std::cout << "camera pose file " << this->cameraparam_save_path << " was loaded." << std::endl;
}

void SequenceViewer::save_camerapose()
{
    this->viewer->saveCameraParameters(this->cameraparam_save_path);
    std::cout << "camera pose file was saved to " << this->cameraparam_save_path << std::endl;
    // Eigen::Affine3f pose_mat;
    // this->viewer->saveCameraParameters("cameraparam.cam");
    // pose_mat = this->viewer->getViewerPose();
    // std::cout << pose_mat.translation() << std::endl;
    // std::cout << pose_mat.rotation() << std::endl;
}

void SequenceViewer::save_screenshot()
{
    this->viewer->saveScreenshot("screenshot_pcl_viewer.png");
    std::cout << "camera pose file was saved to 'screenshot_pcl_viewer.png'" << std::endl;
}

int SequenceViewer::run()
{
    if (pcd_len == 0)
    {
        std::cout << "Point cloud doesn't exist in given path." << std::endl;
        return 1;
    }
    while (!viewer->wasStopped())
    {
        viewer->spinOnce(33);
        if (!this->img_path.empty()) {
            img_viewer->spinOnce(33);
        }
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    }
    return 0;
}

void keyboardEventOccurred(const pcl::visualization::KeyboardEvent &event, void *viewer_void)
{
    SequenceViewer *seq_viewer = static_cast<SequenceViewer *>(viewer_void);

    int new_pcd_id(0);
    if (event.getKeySym() == "Right" && event.keyDown())
    {
        new_pcd_id = (seq_viewer->current_pcd_id + 1) % seq_viewer->pcd_len;
        seq_viewer->update_cloud(new_pcd_id);
    }
    else if (event.getKeySym() == "Left" && event.keyDown())
    {
        new_pcd_id = (seq_viewer->current_pcd_id + seq_viewer->pcd_len - 1) % seq_viewer->pcd_len;
        seq_viewer->update_cloud(new_pcd_id);
    }
    else if (event.getKeySym() == "c" && event.keyDown())
    {
        seq_viewer->save_camerapose();
    }
    else if (event.getKeySym() == "i" && event.keyDown())
    {
        seq_viewer->save_screenshot();
    }
}

void pointPickingEventOccured(const pcl::visualization::PointPickingEvent& event, void *viewer_void)
{
    SequenceViewer *seq_viewer = static_cast<SequenceViewer *>(viewer_void);

    float x,y,z;
    std::cout << event.getPointIndex() << std::endl;
    event.getPoint(x,y,z);
    std::cout << "("<<x<<","<<y<<","<<z<<")" << std::endl; 
}
